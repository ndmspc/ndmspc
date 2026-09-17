#include <gtest/gtest.h>

#include "ndmspc/http/NRoomRouter.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Ndmspc::IRoomCluster;
using Ndmspc::NHttpResponse;
using Ndmspc::NRoomConfig;
using Ndmspc::NRoomRouter;

/// @brief Sets an environment variable for one test and restores it afterwards.
class EnvGuard {
  public:
  EnvGuard(const char * name, const char * value) : fName(name)
  {
    if (const char * old = std::getenv(name); old != nullptr) {
      fHad = true;
      fOld = old;
    }
    if (value != nullptr) ::setenv(name, value, 1);
    else ::unsetenv(name);
  }
  ~EnvGuard()
  {
    if (fHad) ::setenv(fName.c_str(), fOld.c_str(), 1);
    else ::unsetenv(fName.c_str());
  }

  private:
  std::string fName;
  std::string fOld;
  bool        fHad{false};
};

/// @brief An in-memory Kubernetes, so the router's logic can be tested without a cluster.
///
/// It answers the handful of calls the router makes, with the shapes the API server returns, and
/// records what it was asked to do - which is how the tests see the Service and HTTPRoute the
/// router builds.
class FakeCluster : public IRoomCluster {
  public:
  json skeleton = json::object(); ///< What the skeleton ConfigMap holds
  int  readyAfterGets{1};         ///< The Service reports Ready from this GET on (0 = never)
  int  readyGets{0};              ///< How many times its status has been asked for
  bool unschedulable{false};      ///< The room's pod cannot be placed
  bool podsForbidden{false};      ///< The cluster refuses to list pods
  int  replicas{0};               ///< What the latest revision reports

  std::map<std::string, json>                      objects; ///< Path -> stored object
  std::vector<std::pair<std::string, std::string>> calls;   ///< Every (method, path) in order
  std::vector<json>                                bodies;  ///< Every request body, in order

  /// @brief How many times a path was called with a method.
  size_t Count(const std::string & method, const std::string & path) const
  {
    std::lock_guard<std::mutex> lock(fMutex); // a worker may be calling this cluster right now
    size_t                      count = 0;
    for (const auto & call : calls) {
      if (call.first == method && call.second == path) ++count;
    }
    return count;
  }

  /// @brief The body of the last call that matched, or a null json.
  json LastBody(const std::string & method, const std::string & path) const
  {
    std::lock_guard<std::mutex> lock(fMutex);
    json                        found;
    for (size_t i = 0; i < calls.size(); ++i) {
      if (calls[i].first == method && calls[i].second == path) found = bodies[i];
    }
    return found;
  }

  NHttpResponse Request(const std::string & method, const std::string & path, const std::string & body,
                        const std::string & contentType) override
  {
    (void)contentType;
    std::lock_guard<std::mutex> lock(fMutex);
    calls.emplace_back(method, path);
    bodies.push_back(body.empty() ? json() : json::parse(body));

    const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
    const std::string routes   = "/apis/gateway.networking.k8s.io/v1/namespaces/default/httproutes";

    // The skeleton ConfigMap.
    if (path.rfind("/api/v1/namespaces/default/configmaps/", 0) == 0) {
      json configMap;
      configMap["data"]["room-skeleton.json"] = skeleton.dump();
      return Ok(configMap);
    }

    // The room pods: the router only asks whether one is unschedulable.
    if (path.rfind("/api/v1/namespaces/default/pods", 0) == 0) {
      if (podsForbidden) return Status(403, "{\"message\":\"pods is forbidden\"}");
      json items = json::array();
      if (unschedulable) {
        const std::string message = "0/1 nodes are available: 1 Insufficient cpu.";
        json              condition;
        condition["type"]    = "PodScheduled";
        condition["status"]  = "False";
        condition["reason"]  = "Unschedulable";
        condition["message"] = message;

        json pod;
        pod["status"]["conditions"] = json::array({condition});
        items.push_back(pod);
      }
      json list;
      list["items"] = items;
      return Ok(list);
    }

    // The revision's replica count (what room/list reports, and what gates a session capture).
    if (path.rfind("/apis/serving.knative.dev/v1/namespaces/default/revisions/", 0) == 0) {
      json revision;
      revision["status"]["actualReplicas"] = replicas;
      return Ok(revision);
    }

    // A room's Knative Service: created, patched, read back, deleted.
    if (path.rfind(services, 0) == 0) {
      // A GET on the collection is the labelled list the adoption walks; a POST to it creates.
      if (method == "GET" && (path == services || path.rfind(services + "?", 0) == 0)) {
        json items = json::array();
        for (const auto & entry : objects) {
          if (entry.first.rfind(services + "/", 0) == 0 && entry.second.is_object()) items.push_back(entry.second);
        }
        json list;
        list["items"] = items;
        return Ok(list);
      }
      if (method == "DELETE") {
        objects.erase(path);
        return Status(200, "{\"kind\":\"Status\"}");
      }
      if (method == "POST") {
        json        object = json::parse(body);
        std::string key    = services + "/" + object["metadata"]["name"].get<std::string>();
        object["metadata"]["generation"] = 1; // the API server stamps this, not the router
        objects[key]                     = object;
        return Status(201, objects[key].dump());
      }
      if (method == "PATCH") {
        if (!objects.count(path)) return Status(404, "{\"message\":\"not found\"}");
        objects[path].merge_patch(json::parse(body));
        return Ok(objects[path]);
      }
      if (!objects.count(path)) return Status(404, "{\"message\":\"not found\"}");
      if (++readyGets >= readyAfterGets && readyAfterGets > 0) {
        objects[path]["status"]["conditions"]              = json::array(
            {json::object({{"type", "Ready"}, {"status", "True"}})});
        objects[path]["status"]["latestCreatedRevisionName"] = "revision-1";
        objects[path]["status"]["latestReadyRevisionName"]   = "revision-1";
        objects[path]["status"]["observedGeneration"]        = 1;
        return Ok(objects[path]);
      }
      return Ok(objects[path]);
    }

    // A room's HTTPRoute.
    if (path.rfind(routes, 0) == 0) {
      if (method == "DELETE") {
        objects.erase(path);
        return Status(200, "{\"kind\":\"Status\"}");
      }
      if (method == "POST") {
        json object     = json::parse(body);
        std::string key = routes + "/" + object["metadata"]["name"].get<std::string>();
        objects[key]    = object;
        return Status(201, objects[key].dump());
      }
      if (method == "PATCH") {
        if (!objects.count(path)) return Status(404, "{\"message\":\"not found\"}");
        objects[path].merge_patch(json::parse(body));
        return Ok(objects[path]);
      }
      if (!objects.count(path)) return Status(404, "{\"message\":\"not found\"}");
      return Ok(objects[path]);
    }

    return Status(404, "{\"message\":\"not found\"}");
  }

  private:
  mutable std::mutex fMutex; ///< The workers call this cluster while the test reads it
  static NHttpResponse Ok(const json & body)
  {
    NHttpResponse response;
    response.status = 200;
    response.body   = body.dump();
    return response;
  }
  static NHttpResponse Status(int status, const std::string & body)
  {
    NHttpResponse response;
    response.status = status;
    response.body   = body;
    return response;
  }
};

/// @brief A configuration that never waits long, whatever the environment says.
NRoomConfig Config(int readyTimeoutSec = 2)
{
  NRoomConfig cfg;
  cfg.ns              = "default";
  cfg.param           = "room";
  cfg.prefix          = "ndmspc-room-";
  cfg.readyTimeoutSec = readyTimeoutSec;
  cfg.apiServer       = "https://api.test"; // the fake cluster answers whatever the router asks
  return cfg;
}

struct Router {
  std::shared_ptr<FakeCluster> cluster = std::make_shared<FakeCluster>();
  std::unique_ptr<NRoomRouter> router;

  explicit Router(int readyTimeoutSec = 2) : router(std::make_unique<NRoomRouter>(Config(readyTimeoutSec), cluster)) {}

  /// The process's router lives forever; a test's does not, so its workers are stopped first -
  /// closing the rooms cancels them, and nothing may touch the router after it is gone.
  ~Router()
  {
    for (const auto & room : router->Rooms()) {
      json in{{"room", room.second.value}};
      json out;
      router->HandleClose("DELETE", in, out);
    }
    // Closing cancels a creation at its next step; the object must not go before its workers do.
    while (router->Workers() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  /// @brief Drives one action the way the server does.
  json Call(const std::string & action, const std::string & method, json in = json::object())
  {
    json out;
    if (action == "open") router->HandleOpen(method, in, out);
    else if (action == "status") router->HandleStatus(method, in, out);
    else if (action == "list") router->HandleList(method, out);
    else if (action == "close") router->HandleClose(method, in, out);
    else if (action == "backup") router->HandleBackup(method, out);
    else if (action == "restore") router->HandleRestore(method, in, out);
    return out;
  }

  json Open(const std::string & room, bool wait)
  {
    return Call("open", "POST", json({{"room", room}, {"wait", wait}}));
  }

  /// @brief Waits until a room's creation has finished (the worker runs in the background).
  bool WaitFinished(const std::string & room, int seconds = 8)
  {
    for (int i = 0; i < seconds * 20; ++i) {
      const auto rooms = router->Rooms();
      const auto it    = rooms.find(NRoomRouter::RoomName(Config(), room));
      if (it != rooms.end() && !it->second.preparing) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
  }
};

} // namespace

// ---------------------------------------------------------------- configuration

TEST(NRoomConfigTest, ParsesBooleans)
{
  EXPECT_TRUE(NRoomConfig::ParseBool("true", false));
  EXPECT_TRUE(NRoomConfig::ParseBool("1", false));
  EXPECT_TRUE(NRoomConfig::ParseBool("ON", false));
  EXPECT_FALSE(NRoomConfig::ParseBool("false", true));
  EXPECT_FALSE(NRoomConfig::ParseBool("0", true));
  EXPECT_FALSE(NRoomConfig::ParseBool("off", true));
  EXPECT_TRUE(NRoomConfig::ParseBool("something-else", true)); // keeps the fallback
  EXPECT_FALSE(NRoomConfig::ParseBool("", false));
}

TEST(NRoomConfigTest, ParsesDurations)
{
  EXPECT_EQ(NRoomConfig::ParseDuration("45s", 7), 45);
  EXPECT_EQ(NRoomConfig::ParseDuration("1h", 7), 3600);
  EXPECT_EQ(NRoomConfig::ParseDuration("2m", 7), 120);
  EXPECT_EQ(NRoomConfig::ParseDuration("1d", 7), 86400);
  EXPECT_EQ(NRoomConfig::ParseDuration("30", 7), 30);
  EXPECT_EQ(NRoomConfig::ParseDuration("nonsense", 7), 7);
  EXPECT_EQ(NRoomConfig::ParseDuration("", 7), 7);
}

TEST(NRoomConfigTest, ReadsTheEnvironment)
{
  EnvGuard prefix("NDMSPC_ROOM_PREFIX", "room-");
  EnvGuard param("NDMSPC_ROOM_PARAM", "r");
  EnvGuard ttl("NDMSPC_ROOM_IDLE_TTL", "2h");
  EnvGuard wait("NDMSPC_ROOM_WAIT", "false");
  EnvGuard preparing("NDMSPC_ROOM_MAX_PREPARING", "9");
  EnvGuard host("KUBERNETES_SERVICE_HOST", "10.0.0.1");
  EnvGuard port("KUBERNETES_SERVICE_PORT", "6443");

  const NRoomConfig cfg = NRoomConfig::FromEnv();
  EXPECT_EQ(cfg.prefix, "room-");
  EXPECT_EQ(cfg.param, "r");
  EXPECT_EQ(cfg.idleTtlSec, 7200);
  EXPECT_FALSE(cfg.waitDefault);
  EXPECT_EQ(cfg.maxPreparing, 9);
  EXPECT_EQ(cfg.apiServer, "https://10.0.0.1:6443");
}

TEST(NRoomConfigTest, WithoutAKubernetesEnvironmentThereIsNoApiServer)
{
  EnvGuard host("KUBERNETES_SERVICE_HOST", nullptr);
  EXPECT_TRUE(NRoomConfig::FromEnv().apiServer.empty());
}

TEST(NRoomConfigTest, AvailabilityIsTheInClusterApiServerAndSaysWhyNot)
{
  EnvGuard    host("KUBERNETES_SERVICE_HOST", nullptr);
  std::string reason;
  EXPECT_FALSE(NRoomRouter::KubernetesAvailable(&reason));
  EXPECT_FALSE(reason.empty()); // the caller logs this and refuses to serve rooms

  reason.clear();
  {
    EnvGuard inCluster("KUBERNETES_SERVICE_HOST", "10.0.0.1");
    EXPECT_TRUE(NRoomRouter::KubernetesAvailable(&reason));
    EXPECT_TRUE(reason.empty()); // nothing to say when rooms can work
    EXPECT_TRUE(NRoomRouter::KubernetesAvailable()); // the reason is optional
  }

  EXPECT_FALSE(NRoomRouter::KubernetesAvailable());
}

// ---------------------------------------------------------------- the rules

TEST(NRoomRouterTest, SlugIsADnsLabelAndFallsBackToAHash)
{
  EXPECT_EQ(NRoomRouter::Slug("My Room", 63), "my-room");
  EXPECT_EQ(NRoomRouter::Slug("a--b", 63), "a-b"); // a run of dashes collapses to one
  EXPECT_EQ(NRoomRouter::Slug("--x--", 63), "x");
  EXPECT_EQ(NRoomRouter::Slug("abcdef", 4), "abcd");
  EXPECT_EQ(NRoomRouter::Slug("abcd-", 4), "abcd"); // no trailing dash survives truncation

  const std::string hashed = NRoomRouter::Slug("***", 63); // nothing usable left
  EXPECT_EQ(hashed.size(), 17u);                           // "r" + 16 hex digits
  EXPECT_EQ(hashed[0], 'r');
  EXPECT_EQ(hashed, NRoomRouter::Slug("***", 63)); // and it is stable
}

TEST(NRoomRouterTest, NameIsThePrefixPlusTheSlug)
{
  NRoomConfig cfg = Config();
  EXPECT_EQ(NRoomRouter::RoomName(cfg, "My Room"), "ndmspc-room-my-room");

  cfg.prefix = std::string(62, 'p'); // leaves room for one character
  EXPECT_EQ(NRoomRouter::RoomName(cfg, "My Room"), std::string(62, 'p') + "m");
}

TEST(NRoomRouterTest, OnlyObjectsCarryingTheRoomLabelCountAsRooms)
{
  // What the router creates: both objects are stamped with the room label.
  const json service = NRoomRouter::ServiceObject(Config(), "ndmspc-room-test", "test", "http://router.svc:80", json::object());
  EXPECT_TRUE(NRoomRouter::HasRoomLabel(service));
  const json route = NRoomRouter::RouteObject(Config(), "ndmspc-room-test", "test", "revision-1", json::object());
  EXPECT_TRUE(NRoomRouter::HasRoomLabel(route));

  // Anything else is not a room, however it is called.
  json foreign;
  foreign["apiVersion"]        = "serving.knative.dev/v1";
  foreign["kind"]              = "Service";
  foreign["metadata"]["name"]  = "ndmspc-room-router";
  EXPECT_FALSE(NRoomRouter::HasRoomLabel(foreign));

  json emptyLabels;
  emptyLabels["metadata"]["labels"] = json::object();
  EXPECT_FALSE(NRoomRouter::HasRoomLabel(emptyLabels));

  EXPECT_FALSE(NRoomRouter::HasRoomLabel(json::object()));
  EXPECT_FALSE(NRoomRouter::HasRoomLabel(json()));
  EXPECT_FALSE(NRoomRouter::HasRoomLabel(json("a string")));

  // The label itself marks ownership, an empty value included.
  json labelled = foreign;
  labelled["metadata"]["labels"]["ndmspc.io/room"] = "";
  EXPECT_TRUE(NRoomRouter::HasRoomLabel(labelled));

  EXPECT_STREQ(NRoomRouter::kNameConflict, "name_conflict");
}

TEST(NRoomRouterTest, ClientUrlCarriesTheRoomParameter)
{
  NRoomConfig cfg = Config();
  EXPECT_EQ(NRoomRouter::ClientUrl(cfg, "test"), "?room=test");

  cfg.urlBase = "https://ndmspc.example.org";
  EXPECT_EQ(NRoomRouter::ClientUrl(cfg, "test"), "https://ndmspc.example.org/?room=test");
}

TEST(NRoomRouterTest, QueryParsingDecodesAndKeepsEmptyValues)
{
  const auto params = NRoomRouter::ParseQuery("room=my%20room&flag&empty=&plus=a+b");
  ASSERT_EQ(params.size(), 4u);
  EXPECT_EQ(params.at("room"), "my room");
  EXPECT_EQ(params.at("flag"), "");
  EXPECT_EQ(params.at("empty"), "");
  EXPECT_EQ(params.at("plus"), "a b");
  EXPECT_EQ(NRoomRouter::ParseQuery("").size(), 0u);
}

TEST(NRoomRouterTest, RoomParameterIgnoresAnEmptyValue)
{
  EXPECT_EQ(NRoomRouter::RoomParameter("room=alpha&x=1", "room"), "alpha");
  EXPECT_EQ(NRoomRouter::RoomParameter("x=1", "room"), "");
  EXPECT_EQ(NRoomRouter::RoomParameter("room=", "room"), "");
  EXPECT_EQ(NRoomRouter::RoomParameter("r=alpha", "r"), "alpha");
}

TEST(NRoomRouterTest, ServiceObjectStampsTheRoomAndTheSkeletonWins)
{
  json skeleton;
  skeleton["serviceSpec"]["template"]["spec"]["containers"] = json::array({json::object()});
  skeleton["serviceSpec"]["template"]["spec"]["containers"][0]["env"] =
      json::array({json::object({{"name", "NDMSPC_ROOM"}, {"value", "preset"}})});

  const json service =
      NRoomRouter::ServiceObject(Config(), "ndmspc-room-alpha", "alpha", "http://router.default.svc:80", skeleton);

  EXPECT_EQ(service["apiVersion"], "serving.knative.dev/v1");
  EXPECT_EQ(service["kind"], "Service");
  EXPECT_EQ(service["metadata"]["name"], "ndmspc-room-alpha");
  EXPECT_EQ(service["metadata"]["namespace"], "default");
  EXPECT_EQ(service["metadata"]["labels"]["ndmspc.io/room"], "alpha");

  const json env = service["spec"]["template"]["spec"]["containers"][0]["env"];
  std::map<std::string, std::string> values;
  for (const auto & entry : env) values[entry["name"]] = entry["value"];

  EXPECT_EQ(values["NDMSPC_ROOM"], "preset"); // what the skeleton sets wins
  EXPECT_EQ(values["NDMSPC_ROOM_STATE_URL"], "http://router.default.svc:80");
}

TEST(NRoomRouterTest, RouteObjectPinsTheRoomQueryAndTheRevision)
{
  const json skeleton = {{"routeParentRef", {{"name", "knative-gateway"}, {"namespace", "istio-system"}}},
                         {"routeHostname", "ndmspc.example.org"}};

  const json route =
      NRoomRouter::RouteObject(Config(), "ndmspc-room-alpha", "alpha", "ndmspc-room-alpha-00001", skeleton);

  EXPECT_EQ(route["apiVersion"], "gateway.networking.k8s.io/v1");
  EXPECT_EQ(route["kind"], "HTTPRoute");
  EXPECT_EQ(route["metadata"]["namespace"], "default");
  EXPECT_EQ(route["spec"]["hostnames"][0], "ndmspc.example.org");
  EXPECT_EQ(route["spec"]["parentRefs"][0]["name"], "knative-gateway");

  ASSERT_EQ(route["spec"]["rules"].size(), 1u);
  const json rule = route["spec"]["rules"][0];
  ASSERT_EQ(rule["matches"].size(), 1u);
  EXPECT_EQ(rule["matches"][0]["queryParams"][0]["name"], "room");
  EXPECT_EQ(rule["matches"][0]["queryParams"][0]["value"], "alpha");
  EXPECT_EQ(rule["backendRefs"][0]["name"], "ndmspc-room-alpha-00001");
  EXPECT_EQ(rule["backendRefs"][0]["port"], 80);

  const json headers = rule["filters"][0]["requestHeaderModifier"]["set"];
  std::map<std::string, std::string> values;
  for (const auto & entry : headers) values[entry["name"]] = entry["value"];
  EXPECT_EQ(values["Knative-Serving-Namespace"], "default");
  EXPECT_EQ(values["Knative-Serving-Revision"], "ndmspc-room-alpha-00001");
}

TEST(NRoomRouterTest, WaitFlagReadsTheBodyTheQueryAndTheDefault)
{
  NRoomConfig cfg = Config();
  cfg.waitDefault = true;

  EXPECT_TRUE(NRoomRouter::WaitFlag(cfg, json::object()));
  EXPECT_FALSE(NRoomRouter::WaitFlag(cfg, json({{"wait", false}})));
  EXPECT_TRUE(NRoomRouter::WaitFlag(cfg, json({{"wait", true}})));
  EXPECT_FALSE(NRoomRouter::WaitFlag(cfg, json({{"wait", 0}})));
  EXPECT_FALSE(NRoomRouter::WaitFlag(cfg, json({{"wait", "false"}})));
  EXPECT_FALSE(NRoomRouter::WaitFlag(cfg, json({{"_query", "wait=0"}})));

  cfg.waitDefault = false;
  EXPECT_FALSE(NRoomRouter::WaitFlag(cfg, json::object()));
  EXPECT_TRUE(NRoomRouter::WaitFlag(cfg, json({{"_query", "wait=1"}})));
}

TEST(NRoomRouterTest, TheUnschedulableFailureCarriesTheSchedulerMessageAndItsCode)
{
  const std::string reason = "0/1 nodes are available: 1 Insufficient cpu.";
  const std::string error  = NRoomRouter::UnschedulableError(reason);

  EXPECT_NE(error.find(reason), std::string::npos);
  EXPECT_NE(error.find("cannot schedule"), std::string::npos);
  EXPECT_NE(error.find("free capacity"), std::string::npos);
  EXPECT_STREQ(NRoomRouter::kNoCapacity, "no_capacity");
}

// ---------------------------------------------------------------- creating a room

TEST(NRoomRouterActionsTest, OpenCreatesTheServiceAndTheRouteAndWaitsForReady)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}},
                            {"routeHostname", "ndmspc.example.org"}};

  const json out = test.Open("alpha", /*wait=*/true);

  ASSERT_EQ(out["result"], "success");
  EXPECT_EQ(out["payload"]["room"], "alpha");
  EXPECT_EQ(out["payload"]["name"], "ndmspc-room-alpha");
  EXPECT_EQ(out["payload"]["revision"], "revision-1");
  EXPECT_EQ(out["payload"]["state"], "ready");
  EXPECT_EQ(out["payload"]["url"], "?room=alpha");

  const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
  const std::string routes   = "/apis/gateway.networking.k8s.io/v1/namespaces/default/httproutes";
  EXPECT_EQ(test.cluster->Count("POST", services), 1u);
  EXPECT_EQ(test.cluster->Count("POST", routes), 1u);
  EXPECT_FALSE(test.cluster->LastBody("POST", routes).is_null());
  EXPECT_TRUE(test.router->Tracked("alpha"));
  EXPECT_FALSE(test.router->Preparing("alpha"));
}

TEST(NRoomRouterActionsTest, ANameHeldBySomethingThatIsNotARoomIsRefusedNeverOverwritten)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // A Service holding the name the room "router" resolves to, without the room label - which is
  // exactly how the router's own entry Service used to be named (ndmspc-room- + "router").
  const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
  const std::string entry    = services + "/ndmspc-room-router";
  json              foreign;
  foreign["apiVersion"]                             = "serving.knative.dev/v1";
  foreign["kind"]                                   = "Service";
  foreign["metadata"]["name"]                       = "ndmspc-room-router";
  foreign["spec"]["template"]["spec"]["containers"] = json::array({json({{"image", "ndmspc/base:entry"}})});
  test.cluster->objects[entry]                      = foreign;
  const json before                                 = test.cluster->objects.at(entry);

  test.Open("router", /*wait=*/false);
  ASSERT_TRUE(test.WaitFinished("router"));

  const json status = test.Call("status", "GET", json({{"room", "router"}}));
  ASSERT_EQ(status["result"], "success");
  EXPECT_EQ(status["payload"]["state"], "failed");
  EXPECT_EQ(status["payload"].value("code", std::string()), NRoomRouter::kNameConflict);
  EXPECT_NE(status["payload"]["error"].get<std::string>().find("ndmspc-room-router"), std::string::npos);

  // The object was neither patched nor replaced. Compared on the spec: the fake cluster stamps
  // status into whatever it serves, so the whole object is not a stable thing to compare.
  EXPECT_EQ(test.cluster->Count("PATCH", entry), 0u);
  EXPECT_EQ(test.cluster->Count("POST", services), 0u);
  ASSERT_EQ(test.cluster->objects.count(entry), 1u);
  EXPECT_EQ(test.cluster->objects.at(entry)["spec"], before["spec"]);

  // And closing the room does not take it with it.
  test.Call("close", "DELETE", json({{"room", "router"}}));
  EXPECT_EQ(test.cluster->Count("DELETE", entry), 0u);
  ASSERT_EQ(test.cluster->objects.count(entry), 1u);
  EXPECT_EQ(test.cluster->objects.at(entry)["spec"], before["spec"]);
}

TEST(NRoomRouterActionsTest, CloseLeavesAServiceThatIsNotARoomAlone)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  ASSERT_EQ(test.Open("alpha", /*wait=*/true)["result"], "success");

  // Whatever now holds the room's name is no longer a room (label gone): it must survive the close.
  const std::string entry = "/apis/serving.knative.dev/v1/namespaces/default/services/ndmspc-room-alpha";
  test.cluster->objects[entry]["metadata"]["labels"] = json::object();

  test.Call("close", "DELETE", json({{"room", "alpha"}}));
  EXPECT_EQ(test.cluster->Count("DELETE", entry), 0u);
  ASSERT_EQ(test.cluster->objects.count(entry), 1u);
}

TEST(NRoomRouterActionsTest, OpenWithWaitFalseAnswersPreparingAndFinishesInTheBackground)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  const json out = test.Open("alpha", /*wait=*/false);
  EXPECT_EQ(out["result"], "success");
  EXPECT_EQ(out["payload"]["state"], "preparing");
  EXPECT_EQ(out["payload"]["phase"], "service");

  EXPECT_TRUE(test.WaitFinished("alpha"));
  const json status = test.Call("status", "GET", json({{"room", "alpha"}}));
  EXPECT_EQ(status["payload"]["state"], "ready");
  EXPECT_EQ(status["payload"]["tracked"], true);
}

TEST(NRoomRouterActionsTest, ASecondOpenWhilePreparingIsReportedNotRestarted)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.cluster->readyAfterGets = 0; // never ready: the creation stays in flight

  const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
  EXPECT_EQ(test.Open("alpha", false)["payload"]["state"], "preparing");
  for (int i = 0; i < 40 && test.cluster->Count("POST", services) == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // the worker is creating it
  }

  const json again = test.Open("alpha", false);
  EXPECT_EQ(again["payload"]["state"], "preparing");
  EXPECT_FALSE(again["payload"].value("phase", std::string()).empty()); // wherever the worker is

  // Only one Service was created, and the room is still being prepared.
  EXPECT_EQ(test.cluster->Count("POST", services), 1u);
  EXPECT_TRUE(test.router->Preparing("alpha"));
}

TEST(NRoomRouterActionsTest, APodTheSchedulerCannotPlaceFailsFastWithTheReason)
{
  // Long enough for the verdict to repeat, which is what makes it a verdict.
  Router test(/*readyTimeoutSec=*/5);
  test.cluster->skeleton      = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.cluster->readyAfterGets = 0;
  test.cluster->unschedulable  = true;

  test.Open("alpha", false);
  ASSERT_TRUE(test.WaitFinished("alpha"));

  size_t podQueries = 0;
  for (const auto & call : test.cluster->calls) {
    if (call.second.rfind("/api/v1/namespaces/default/pods", 0) == 0) ++podQueries;
  }
  EXPECT_GE(podQueries, 2u); // the verdict has to repeat before the room fails

  const json status = test.Call("status", "GET", json({{"room", "alpha"}}));
  EXPECT_EQ(status["result"], "success");
  EXPECT_EQ(status["payload"]["state"], "failed");
  EXPECT_EQ(status["payload"]["phase"], "failed");
  EXPECT_EQ(status["payload"].value("code", std::string()), NRoomRouter::kNoCapacity);
  EXPECT_NE(status["payload"]["error"].get<std::string>().find("Insufficient cpu"), std::string::npos);

  const json list = test.Call("list", "GET");
  ASSERT_EQ(list["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(list["payload"]["rooms"][0]["state"], "failed");
  EXPECT_EQ(list["payload"]["rooms"][0]["code"], NRoomRouter::kNoCapacity);
}

TEST(NRoomRouterActionsTest, AClusterThatRefusesToShowPodsStillReportsATimeout)
{
  Router test(/*readyTimeoutSec=*/1);
  test.cluster->skeleton      = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.cluster->readyAfterGets = 0;
  test.cluster->podsForbidden  = true;

  test.Open("alpha", false);
  ASSERT_TRUE(test.WaitFinished("alpha"));

  const json status = test.Call("status", "GET", json({{"room", "alpha"}}));
  EXPECT_EQ(status["payload"]["state"], "failed");
  // No reason given: the permission is missing, not the capacity, so there is no code at all.
  EXPECT_EQ(status["payload"].value("code", std::string()), "");
  EXPECT_NE(status["payload"]["error"].get<std::string>().find("timed out"), std::string::npos);
}

TEST(NRoomRouterActionsTest, CloseCancelsACreationAndDeletesWhatItMadeSoFar)
{
  Router test;
  test.cluster->skeleton      = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.cluster->readyAfterGets = 0; // keep it in flight

  const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
  const std::string entry    = services + "/ndmspc-room-alpha";

  test.Open("alpha", false);
  ASSERT_TRUE(test.router->Preparing("alpha"));

  // Wait for the Service the worker makes: the close below deletes what is really there, so the
  // assertion is about an object that exists rather than about a blind delete call.
  for (int i = 0; i < 200 && test.cluster->Count("POST", services) == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_EQ(test.cluster->Count("POST", services), 1u);

  const json out = test.Call("close", "DELETE", json({{"room", "alpha"}}));
  EXPECT_EQ(out["result"], "success");

  EXPECT_EQ(test.cluster->Count("DELETE", entry), 1u);
  EXPECT_EQ(test.cluster->objects.count(entry), 0u);
  EXPECT_FALSE(test.router->Tracked("alpha"));

  // The worker notices at its next step: nothing is left behind and the room stays gone.
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  EXPECT_FALSE(test.router->Tracked("alpha"));
  EXPECT_TRUE(test.Call("list", "GET")["payload"]["rooms"].empty());
}

TEST(NRoomRouterActionsTest, RestoreEnsuresTheRoomsOfADocumentAdditively)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  json document;
  document["version"] = 1;
  document["rooms"]   = json::array({json::object({{"room", "alpha"}, {"snapshot", {{"v", 1}, {"file", "x.root"}}}})});

  const json out = test.Call("restore", "POST", document);
  EXPECT_EQ(out["result"], "success");
  // The room is ensured and carries its session; replaying it means talking to the room, which a
  // fake cluster cannot answer - so it is reported per room rather than losing the restore.
  ASSERT_EQ(out["payload"]["restored"].size() + out["payload"]["failed"].size(), 1u);
  EXPECT_TRUE(test.router->Tracked("alpha"));

  // The session came with it, and room/state can be asked for it.
  const json state = test.Call("status", "GET", json({{"room", "alpha"}}));
  EXPECT_EQ(state["payload"]["hasSnapshot"], true);
}

TEST(NRoomRouterActionsTest, AdoptsTheRoomsTheClusterAlreadyHas)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  json service;
  service["metadata"]["name"]     = "ndmspc-room-old";
  service["metadata"]["namespace"] = "default";
  service["metadata"]["labels"]["ndmspc.io/room"] = "old";
  test.cluster->objects["/apis/serving.knative.dev/v1/namespaces/default/services/ndmspc-room-old"] = service;

  // Asking for a room adopts the ones the cluster already has before answering.
  test.Open("alpha", true);

  EXPECT_TRUE(test.router->Tracked("old"));
  const json list = test.Call("list", "GET");
  ASSERT_EQ(list["payload"]["rooms"].size(), 2u);

  std::vector<std::string> rooms;
  for (const auto & room : list["payload"]["rooms"]) rooms.push_back(room["room"]);
  EXPECT_NE(std::find(rooms.begin(), rooms.end(), "old"), rooms.end());
  EXPECT_NE(std::find(rooms.begin(), rooms.end(), "alpha"), rooms.end());
}

TEST(NRoomRouterActionsTest, TheWebsocketPolicyNeedsATrackedRoom)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  EXPECT_FALSE(test.router->WsConnect(""));
  EXPECT_FALSE(test.router->WsConnect("room=nosuch"));
  EXPECT_FALSE(test.router->WsConnect("room="));

  test.Open("alpha", true);
  EXPECT_TRUE(test.router->WsConnect("room=alpha"));
  EXPECT_FALSE(test.router->WsConnect("room=nosuch"));
}

TEST(NRoomRouterActionsTest, TheMethodsEachActionAcceptsAreChecked)
{
  Router test;

  const json opened = test.Call("open", "DELETE", json({{"room", "alpha"}}));
  EXPECT_EQ(opened["result"], "failure");
  EXPECT_NE(opened["error"].get<std::string>().find("Unsupported HTTP method"), std::string::npos);

  const json listed = test.Call("list", "POST");
  EXPECT_EQ(listed["result"], "failure");

  const json closed = test.Call("close", "GET", json({{"room", "alpha"}}));
  EXPECT_EQ(closed["result"], "failure");

  const json openNoRoom = test.Call("open", "POST", json::object());
  EXPECT_EQ(openNoRoom["result"], "failure");
  EXPECT_NE(openNoRoom["error"].get<std::string>().find("Missing room id"), std::string::npos);
}
