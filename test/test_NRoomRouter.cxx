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
using Ndmspc::NHttpServer;
using Ndmspc::NRequestIdentity;
using Ndmspc::NRoomAccess;
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
NRoomConfig Config(int readyTimeoutSec = 2, std::vector<std::string> admins = {})
{
  NRoomConfig cfg;
  cfg.ns              = "default";
  cfg.param           = "room";
  cfg.prefix          = "ndmspc-room-";
  cfg.readyTimeoutSec = readyTimeoutSec;
  cfg.admins          = std::move(admins);
  cfg.apiServer       = "https://api.test"; // the fake cluster answers whatever the router asks
  return cfg;
}

/// @brief The input of a request whose caller the server verified (what `_identity` carries).
json Verified(const std::string & username, const std::string & email = "", json in = json::object())
{
  in["_identity"] = {{"user", username}, {"email", email}, {"subject", "subject-" + username}, {"verified", true}};
  return in;
}

/// @brief The input of a request that only says who it is (a deployment with no login at all).
json Asserted(const std::string & owner, json in = json::object())
{
  in["owner"] = owner;
  return in;
}

struct Router {
  std::shared_ptr<FakeCluster> cluster = std::make_shared<FakeCluster>();
  std::unique_ptr<NRoomRouter> router;

  explicit Router(int readyTimeoutSec = 2, std::vector<std::string> admins = {})
      : router(std::make_unique<NRoomRouter>(Config(readyTimeoutSec, std::move(admins)), cluster))
  {
  }

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
    else if (action == "list") router->HandleList(method, in, out);
    else if (action == "close") router->HandleClose(method, in, out);
    else if (action == "backup") router->HandleBackup(method, in, out);
    else if (action == "restore") router->HandleRestore(method, in, out);
    return out;
  }

  json Open(const std::string & room, bool wait)
  {
    return OpenBy(json::object(), room, wait);
  }

  /// @brief Opens a room as a caller: `extra` carries the identity (or the asserted owner).
  json OpenBy(json extra, const std::string & room, bool wait = false)
  {
    extra["room"] = room;
    extra["wait"] = wait;
    return Call("open", "POST", std::move(extra));
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

TEST(NRoomConfigTest, ReadsTheAdminList)
{
  EnvGuard admins("NDMSPC_ROOM_ADMINS", " alice@example.com , bob ");
  const NRoomConfig cfg = NRoomConfig::FromEnv();
  ASSERT_EQ(cfg.admins.size(), 2u);
  EXPECT_EQ(cfg.admins[0], "alice@example.com"); // surrounding space is not an identity
  EXPECT_EQ(cfg.admins[1], "bob");

  // An empty (or absent) list means nobody is an admin, which is the default.
  EnvGuard none("NDMSPC_ROOM_ADMINS", "");
  EXPECT_TRUE(NRoomConfig::FromEnv().admins.empty());
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
  const json service =
      NRoomRouter::ServiceObject(Config(), "ndmspc-room-test", "test", "http://router.svc:80", json::object(), "",
                                 json::object());
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

  // With a token the link opens the room, which is what a client hands on.
  EXPECT_EQ(NRoomRouter::ClientUrl(cfg, "test", "abc123"), "https://ndmspc.example.org/?room=test&token=abc123");
}

TEST(NRoomRouterTest, TokensAreRandomAndCarryALevel)
{
  const std::string first  = NRoomRouter::NewRoomToken();
  const std::string second = NRoomRouter::NewRoomToken();
  EXPECT_EQ(first.size(), 32u);
  EXPECT_EQ(second.size(), 32u);
  EXPECT_NE(first, second); // two rooms must never share a token

  const json access = NRoomRouter::AccessJson("rw-token", "ro-token");
  EXPECT_EQ(access["rw"], "rw-token");
  EXPECT_EQ(access["ro"], "ro-token");
  EXPECT_TRUE(NRoomRouter::AccessJson("", "").empty());
}

TEST(NRoomAccessTest, OnlyTheRoomsOwnTokensUnlockItAndTheLevelIsNamed)
{
  const json access = NRoomAccess::Parse(NRoomRouter::AccessJson("rw-token", "ro-token").dump());
  EXPECT_EQ(NRoomAccess::LevelOf(access, "rw-token"), "rw");
  EXPECT_EQ(NRoomAccess::LevelOf(access, "ro-token"), "ro");
  EXPECT_EQ(NRoomAccess::LevelOf(access, "rw-toke"), ""); // a near miss grants nothing
  EXPECT_EQ(NRoomAccess::LevelOf(access, "rw-tokens"), "");
  EXPECT_EQ(NRoomAccess::LevelOf(access, ""), "");

  // No configuration means no enforcement, and nothing to compare against.
  EXPECT_TRUE(NRoomAccess::Parse("").empty());
  EXPECT_TRUE(NRoomAccess::Parse("not json").empty());
  EXPECT_TRUE(NRoomAccess::Parse("[1,2,3]").empty());
  EXPECT_EQ(NRoomAccess::LevelOf(json::object(), "rw-token"), "");
}

TEST(NRoomAccessTest, AServerEnforcesAccessOnlyWhenItWasGivenTokens)
{
  // The switch is the environment variable, not the code: a server that was given no tokens (the
  // router itself, an older image, a room created before access existed) refuses nothing.
  {
    EnvGuard    access("NDMSPC_ROOM_ACCESS", nullptr);
    NHttpServer server("", /*ws=*/false, 10000, {}, /*startEngine=*/false);
    EXPECT_FALSE(server.RoomAccessRequired());
    EXPECT_TRUE(server.RoomAccessTokens().empty());
  }

  // ... and a room that was given them enforces them, at the levels it was given.
  {
    const std::string tokens = NRoomRouter::AccessJson("rw-token", "ro-token").dump();
    EnvGuard          access("NDMSPC_ROOM_ACCESS", tokens.c_str());
    NHttpServer       server("", /*ws=*/false, 10000, {}, /*startEngine=*/false);
    EXPECT_TRUE(server.RoomAccessRequired());
    EXPECT_EQ(server.RoomAccessLevel("rw-token"), "rw");
    EXPECT_EQ(server.RoomAccessLevel("ro-token"), "ro");
    EXPECT_EQ(server.RoomAccessLevel("rw-tokens"), "");
    EXPECT_EQ(server.RoomAccessLevel(""), "");
  }
}

TEST(NRoomAccessTest, TheTokenTravelsInTheLinkInAHeaderOrInTheRoomsCookie)
{
  // A link: what a page handed out carries.
  EXPECT_EQ(NRoomAccess::TokenFromQuery("room=alpha&token=abc"), "abc");
  EXPECT_EQ(NRoomAccess::TokenFromQuery("?token=abc&room=alpha"), "abc");
  EXPECT_EQ(NRoomAccess::TokenFromQuery("room=alpha"), "");
  EXPECT_EQ(NRoomAccess::TokenFromQuery("room=alpha&tokenish=abc"), ""); // only the exact name counts
  EXPECT_EQ(NRoomAccess::TokenFromQuery("room=alpha&token="), "");      // an empty value is no token
  EXPECT_EQ(NRoomAccess::TokenFromQuery(""), "");

  // A cookie: what the room's page gives the browser, since the page's own scripts cannot add a
  // header to their API and websocket calls.
  EXPECT_EQ(NRoomAccess::TokenFromCookie("ndmspc-room-access=abc"), "abc");
  EXPECT_EQ(NRoomAccess::TokenFromCookie("other=1; ndmspc-room-access=abc; more=2"), "abc");
  EXPECT_EQ(NRoomAccess::TokenFromCookie("other=1"), "");
  EXPECT_EQ(NRoomAccess::TokenFromCookie("ndmspc-room-access-extra=abc"), "");
  EXPECT_EQ(NRoomAccess::TokenFromCookie(""), "");
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

  const json access = json::object({{"rw", "rw-token"}, {"ro", "ro-token"}});
  const json service =
      NRoomRouter::ServiceObject(Config(), "ndmspc-room-alpha", "alpha", "http://router.default.svc:80", access,
                                 "alice@example.com", skeleton);

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

  // The tokens reach the room, and a copy stays on the Service so that a router restart - and an
  // idle room coming back - still know which links open it.
  EXPECT_EQ(values["NDMSPC_ROOM_ACCESS"], access.dump());
  EXPECT_EQ(service["metadata"]["annotations"]["ndmspc.io/room-access"], access.dump());
  // The owner is kept beside the tokens and for the same reason: a restart, and an idle room waking
  // up, must not lose who the room belongs to.
  EXPECT_EQ(service["metadata"]["annotations"]["ndmspc.io/room-owner"], "alice@example.com");

  // A room handed no tokens enforces nothing, so it must not get an empty switch either.
  const json bare =
      NRoomRouter::ServiceObject(Config(), "ndmspc-room-beta", "beta", "http://router.default.svc:80",
                                 json::object(), "", skeleton);
  std::map<std::string, std::string> bareValues;
  for (const auto & entry : bare["spec"]["template"]["spec"]["containers"][0]["env"]) {
    bareValues[entry["name"]] = entry["value"];
  }
  EXPECT_EQ(bareValues.count("NDMSPC_ROOM_ACCESS"), 0u);
  // No tokens means no access annotation - but the id is always there, since a label value cannot
  // hold one (it is a slug of the id) and the router has to be able to read the room back.
  EXPECT_FALSE(bare["metadata"]["annotations"].contains("ndmspc.io/room-access"));
  EXPECT_EQ(bare["metadata"]["annotations"]["ndmspc.io/room-id"], "beta");
  EXPECT_EQ(bare["metadata"]["labels"]["ndmspc.io/room"], "beta");
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
  // The url is the link a client hands on, so it carries the room's read-write token.
  const std::string tokenRw = out["payload"]["access"]["rw"].get<std::string>();
  const std::string tokenRo = out["payload"]["access"]["ro"].get<std::string>();
  EXPECT_EQ(out["payload"]["url"], "?room=alpha&token=" + tokenRw);
  EXPECT_EQ(tokenRw.size(), 32u);
  EXPECT_EQ(tokenRo.size(), 32u);
  EXPECT_NE(tokenRw, tokenRo); // the two levels are separate secrets

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

// ---------------------------------------------------------------- ownership and admins

TEST(NRoomOwnershipTest, ANewRoomBelongsToWhoeverCreatedIt)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // The verified identity names the room: the user name it is called by, not its address.
  const json opened = test.OpenBy(Verified("alice", "alice@example.com"), "alpha", /*wait=*/true);
  ASSERT_EQ(opened["result"], "success");
  EXPECT_EQ(opened["payload"]["owner"], "alice");

  // And it is kept on the room's own Service, beside its tokens, so a router restart does not lose
  // it.
  const json service =
      test.cluster->LastBody("POST", "/apis/serving.knative.dev/v1/namespaces/default/services");
  EXPECT_EQ(service["metadata"]["annotations"]["ndmspc.io/room-owner"], "alice");
}

TEST(NRoomOwnershipTest, ARoomKeepsTheOwnerItWasCreatedWith)
{
  // root is an admin, so opening someone else's room is allowed and reports the room as it is.
  Router test(2, {"root@example.com"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  test.OpenBy(Verified("alice", "alice@example.com"), "alpha", /*wait=*/true);
  const json reopened =
      test.OpenBy(Verified("root", "root@example.com"), "alice-alpha", /*wait=*/true);
  ASSERT_EQ(reopened["result"], "success");
  EXPECT_EQ(reopened["payload"]["owner"], "alice"); // still hers, not the admin's
  EXPECT_EQ(reopened["payload"]["created"], false);
}

TEST(NRoomOwnershipTest, AnIdentifiedCallerSeesTheirOwnRoomsAndAnAdminSeesAll)
{
  Router test(2, {"boss@example.com"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  test.OpenBy(Verified("alice", "alice@example.com"), "alpha", /*wait=*/true);
  test.OpenBy(Verified("bob", "bob@example.com"), "beta", /*wait=*/true);
  test.OpenBy(json::object(), "legacy", /*wait=*/true); // created by nobody identifiable: nobody's

  const json admin = test.Call("list", "GET", Verified("boss", "boss@example.com"));
  EXPECT_EQ(admin["payload"]["rooms"].size(), 3u);
  // And it says why: this caller is one of the admins, so the list is everyone's.
  EXPECT_EQ(admin["payload"]["admin"], true);

  const json mine = test.Call("list", "GET", Verified("alice", "alice@example.com"));
  ASSERT_EQ(mine["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(mine["payload"]["admin"], false);
  // The id of a room an identified caller made carries them: that is what makes it theirs alone.
  EXPECT_EQ(mine["payload"]["rooms"][0]["room"], "alice-alpha");
  EXPECT_EQ(mine["payload"]["rooms"][0]["owner"], "alice");

  const json theirs = test.Call("list", "GET", Verified("bob", "bob@example.com"));
  ASSERT_EQ(theirs["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(theirs["payload"]["rooms"][0]["room"], "bob-beta");

  // A caller that says nothing about itself is answered as it always was: the whole registry, the
  // room with no owner included. That is what a script and the TUI are - and it is not an admin, so
  // a view does not claim to be one on its behalf.
  const json anonymous = test.Call("list", "GET");
  EXPECT_EQ(anonymous["payload"]["rooms"].size(), 3u);
  EXPECT_EQ(anonymous["payload"]["admin"], false);
}

TEST(NRoomOwnershipTest, AnAdminIsMatchedByEmailOrUserNameCaseInsensitively)
{
  // The list may name people by email or by user name; a caller matches on either.
  Router test(2, {"Boss@Example.COM", "root"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.OpenBy(Verified("alice", "alice@example.com"), "alpha", /*wait=*/true);

  // By email, whatever the case it is written in.
  EXPECT_EQ(test.Call("list", "GET", Verified("boss", "boss@example.com"))["payload"]["rooms"].size(), 1u);
  // By user name, for a deployment whose tokens carry no email.
  EXPECT_EQ(test.Call("list", "GET", Verified("root", "root@example.com"))["payload"]["rooms"].size(), 1u);
  // Someone in neither stays limited to their own rooms.
  EXPECT_EQ(test.Call("list", "GET", Verified("carol", "carol@example.com"))["payload"]["rooms"].size(), 0u);
}

TEST(NRoomOwnershipTest, AnAssertedIdentityCanClaimAnEmailForTheAdminList)
{
  // A client may know both names for itself - the UI sends the signed-in user's user name and email -
  // so an admin list written in emails recognises it while its rooms are still named after its user
  // name. Without that, a signed-in admin would look like a stranger to the router.
  Router test(2, {"boss@example.com"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  const json both = Asserted("boss", {{"owner_email", "boss@example.com"}});

  const json list = test.Call("list", "GET", both);
  ASSERT_EQ(list["result"], "success");
  EXPECT_EQ(list["payload"]["admin"], true);

  const json opened = test.OpenBy(both, "mine", /*wait=*/true);
  ASSERT_EQ(opened["result"], "success");
  EXPECT_EQ(opened["payload"]["room"], "boss-mine");
  EXPECT_EQ(opened["payload"]["owner"], "boss");

  // One name alone is still only that name: a user name does not match an email entry on its own.
  EXPECT_EQ(test.Call("list", "GET", Asserted("boss"))["payload"]["admin"], false);
  // And the email, when it is all a client has, is an identity in its own right.
  EXPECT_EQ(test.Call("list", "GET", Asserted("", {{"owner_email", "boss@example.com"}}))["payload"]["admin"], true);
}

TEST(NRoomOwnershipTest, SomeoneElsesRoomIsRefusedNotOnlyHidden)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.OpenBy(Verified("alice", "alice@example.com"), "alpha", /*wait=*/true);
  const std::string room = "alice-alpha";

  // bob may not read it, may not walk into it through room/open (which hands out its link), and may
  // not delete it.
  const json status = test.Call("status", "GET", Verified("bob", "bob@example.com", {{"room", room}}));
  EXPECT_EQ(status["result"], "failure");
  EXPECT_EQ(status.value("code", ""), "not_owner");

  const json opened = test.OpenBy(Verified("bob", "bob@example.com"), room, /*wait=*/true);
  EXPECT_EQ(opened["result"], "failure");
  EXPECT_EQ(opened.value("code", ""), "not_owner");

  const json closed = test.Call("close", "DELETE", Verified("bob", "bob@example.com", {{"room", room}}));
  EXPECT_EQ(closed["result"], "failure");
  EXPECT_EQ(closed.value("code", ""), "not_owner");
  EXPECT_TRUE(test.router->Tracked(room)); // a refusal deletes nothing

  // bob's own "alpha", though, is a room of his own: the id is not alice's to keep from him, only
  // that room is.
  const json bobs = test.Call("status", "GET", Verified("bob", "bob@example.com", {{"room", "alpha"}}));
  EXPECT_EQ(bobs["result"], "success");
  EXPECT_EQ(bobs["payload"]["room"], "bob-alpha");
  EXPECT_FALSE(bobs["payload"]["tracked"].get<bool>());

  // An anonymous caller - a script, or the TUI - keeps working on any room.
  EXPECT_EQ(test.Call("status", "GET", json{{"room", room}})["result"], "success");
}

TEST(NRoomOwnershipTest, AVerifiedCallerBeatsWhatTheRequestAsserts)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // The request claims to be bob; the server knows it is alice, and the assertion is ignored.
  json in       = Verified("alice", "alice@example.com");
  in["owner"]   = "bob@example.com";
  const json opened = test.OpenBy(in, "alpha", /*wait=*/true);
  ASSERT_EQ(opened["result"], "success");
  EXPECT_EQ(opened["payload"]["owner"], "alice");

  // And bob does not get the room by having claimed it - nor by now claiming it again.
  EXPECT_TRUE(test.Call("list", "GET", Asserted("bob@example.com"))["payload"]["rooms"].empty());
  EXPECT_EQ(test.Call("list", "GET", Verified("alice", "alice@example.com"))["payload"]["rooms"].size(), 1u);
}

TEST(NRoomOwnershipTest, WithoutLoginTheOwnerIsWhatTheClientAsserts)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  const json opened = test.OpenBy(Asserted("alice@example.com"), "alpha", /*wait=*/true);
  ASSERT_EQ(opened["result"], "success");
  EXPECT_EQ(opened["payload"]["owner"], "alice@example.com");

  EXPECT_EQ(test.Call("list", "GET", Asserted("alice@example.com"))["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(test.Call("list", "GET", Asserted("bob@example.com"))["payload"]["rooms"].size(), 0u);

  // The assertion travels in the query too, which is how a link carries it.
  const json query = test.Call("list", "GET", json{{"_query", "owner=alice%40example.com"}});
  EXPECT_EQ(query["payload"]["rooms"].size(), 1u);
}

TEST(NRoomOwnershipTest, TheOwnerIsReadBackFromTheRoomsOwnService)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // A room the cluster already has, with its owner and its id on the Service: this is what a router
  // that has just restarted finds.
  const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
  json              service;
  service["metadata"]["name"]      = "ndmspc-room-alice-example-com-mine";
  service["metadata"]["namespace"] = "default";
  // The label holds the id's slug and the annotation the id itself, as the router writes them.
  service["metadata"]["labels"]["ndmspc.io/room"]           = "alice-example-com-mine";
  service["metadata"]["annotations"]["ndmspc.io/room-id"]   = "alice@example.com-mine";
  service["metadata"]["annotations"]["ndmspc.io/room-owner"] = "alice@example.com";
  test.cluster->objects[services + "/ndmspc-room-alice-example-com-mine"] = service;

  test.OpenBy(json::object(), "beta", /*wait=*/true); // asking for any room adopts the cluster's rooms first

  // The id and owner read back are the ones callers are matched against.
  const json status =
      test.Call("status", "GET", Verified("alice", "alice@example.com", {{"room", "alice@example.com-mine"}}));
  ASSERT_EQ(status["result"], "success");
  EXPECT_EQ(status["payload"]["room"], "alice@example.com-mine");
  EXPECT_EQ(status["payload"]["owner"], "alice@example.com");

  const json other =
      test.Call("status", "GET", Verified("bob", "bob@example.com", {{"room", "alice@example.com-mine"}}));
  EXPECT_EQ(other.value("code", ""), "not_owner");
}

TEST(NRoomOwnershipTest, ABackupCarriesTheOwnerAndAScopedBackupOnlyOnesOwnRooms)
{
  Router test(2, {"boss@example.com"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  test.OpenBy(Verified("alice", "alice@example.com"), "alpha", /*wait=*/true);
  test.OpenBy(Verified("bob", "bob@example.com"), "beta", /*wait=*/true);

  const json mine = test.Call("backup", "GET", Verified("alice", "alice@example.com"));
  ASSERT_EQ(mine["result"], "success");
  ASSERT_EQ(mine["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(mine["payload"]["rooms"][0]["room"], "alice-alpha");
  EXPECT_EQ(mine["payload"]["rooms"][0]["owner"], "alice");

  const json all = test.Call("backup", "GET", Verified("boss", "boss@example.com"));
  EXPECT_EQ(all["payload"]["rooms"].size(), 2u);
}

TEST(NRoomOwnershipTest, ARestoreKeepsTheDocumentsOwnerAndRefusesSomeoneElsesRoom)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // carol may restore a document of her own rooms...
  json document;
  document["version"] = 1;
  document["rooms"]   = json::array({json::object({{"room", "mine"}, {"owner", "carol@example.com"}})});
  const json restored = test.Call("restore", "POST", Verified("carol", "carol@example.com", document));
  ASSERT_EQ(restored["result"], "success");
  ASSERT_EQ(restored["payload"]["restored"].size(), 1u);
  EXPECT_EQ(test.Call("list", "GET", Verified("carol", "carol@example.com"))["payload"]["rooms"].size(), 1u);

  // ... but not someone else's, and the refusal names what it would not touch.
  json foreign;
  foreign["version"] = 1;
  foreign["rooms"]   = json::array({json::object({{"room", "mine"}, {"owner", "carol@example.com"}})});
  const json refused = test.Call("restore", "POST", Verified("dave", "dave@example.com", foreign));
  ASSERT_EQ(refused["result"], "success");
  EXPECT_TRUE(refused["payload"]["restored"].empty());
  ASSERT_EQ(refused["payload"]["failed"].size(), 1u);
  EXPECT_EQ(refused["payload"]["failed"][0]["code"], "not_owner");
  // The room is still carol's.
  EXPECT_EQ(test.Call("list", "GET", Verified("carol", "carol@example.com"))["payload"]["rooms"].size(), 1u);
}

// ---------------------------------------------------------------- naming

TEST(NRoomOwnershipTest, QualifyPutsTheKnownOwnerInFrontOfTheId)
{
  // Nobody identified: the id is the name, as it always was.
  EXPECT_EQ(NRoomRouter::Qualify(NRequestIdentity(), "mine"), "mine");

  NRequestIdentity alice;
  alice.subject  = "subject-1";
  alice.username = "alice";
  alice.email    = "alice@example.com";
  alice.verified = true;
  // The user name, not the email: an id is a URL, a label and a resource name at once.
  EXPECT_EQ(NRoomRouter::Qualify(alice, "mine"), "alice-mine");

  // A token with no user name falls back to the email, and a certificate to the name it carries.
  NRequestIdentity emailOnly = alice;
  emailOnly.username.clear();
  EXPECT_EQ(NRoomRouter::Qualify(emailOnly, "mine"), "alice@example.com-mine");
  NRequestIdentity certificate;
  certificate.subject  = "CN=mvala";
  certificate.verified = true;
  EXPECT_EQ(NRoomRouter::Qualify(certificate, "mine"), "CN=mvala-mine");
}

TEST(NRoomOwnershipTest, ARoomIsNamedAfterWhoeverCreatedIt)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  const json opened = test.OpenBy(Verified("alice", "alice@example.com"), "mine", /*wait=*/true);
  ASSERT_EQ(opened["result"], "success");
  // The id it answers with is the one it used, and the same one every other view will show.
  EXPECT_EQ(opened["payload"]["room"], "alice-mine");
  EXPECT_EQ(opened["payload"]["name"], "ndmspc-room-alice-mine");
  EXPECT_EQ(opened["payload"]["owner"], "alice");
  EXPECT_EQ(opened["payload"]["created"], true);
  EXPECT_TRUE(test.router->Tracked("alice-mine"));

  const json service =
      test.cluster->LastBody("POST", "/apis/serving.knative.dev/v1/namespaces/default/services");
  EXPECT_EQ(service["metadata"]["labels"]["ndmspc.io/room"], "alice-mine");
  EXPECT_EQ(service["metadata"]["annotations"]["ndmspc.io/room-id"], "alice-mine");

  // An identity with no user name is named by its email - which is not a Kubernetes label value
  // (no '@'), so the label carries the id's slug and the id itself lives in an annotation.
  const json viaEmail = test.OpenBy(Verified("", "carol@example.com"), "mine", /*wait=*/true);
  ASSERT_EQ(viaEmail["result"], "success");
  EXPECT_EQ(viaEmail["payload"]["room"], "carol@example.com-mine");
  const json emailService =
      test.cluster->LastBody("POST", "/apis/serving.knative.dev/v1/namespaces/default/services");
  EXPECT_EQ(emailService["metadata"]["labels"]["ndmspc.io/room"], "carol-example-com-mine");
  EXPECT_EQ(emailService["metadata"]["annotations"]["ndmspc.io/room-id"], "carol@example.com-mine");
  EXPECT_EQ(emailService["metadata"]["labels"]["ndmspc.io/room"].get<std::string>().find('@'),
            std::string::npos);
}

TEST(NRoomOwnershipTest, TheSameIdIsADifferentRoomForEachOwner)
{
  Router test(2, {"boss@example.com"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  const json alices = test.OpenBy(Verified("alice", "alice@example.com"), "mine", /*wait=*/true);
  const json bobs   = test.OpenBy(Verified("bob", "bob@example.com"), "mine", /*wait=*/true);
  EXPECT_EQ(alices["payload"]["room"], "alice-mine");
  EXPECT_EQ(bobs["payload"]["room"], "bob-mine");
  EXPECT_EQ(bobs["payload"]["owner"], "bob");

  // Each sees their own, and knows it by the qualified id.
  const json alice = test.Call("list", "GET", Verified("alice", "alice@example.com"));
  ASSERT_EQ(alice["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(alice["payload"]["rooms"][0]["room"], "alice-mine");
  const json bob = test.Call("list", "GET", Verified("bob", "bob@example.com"));
  ASSERT_EQ(bob["payload"]["rooms"].size(), 1u);
  EXPECT_EQ(bob["payload"]["rooms"][0]["room"], "bob-mine");

  // And the admin sees both of them.
  EXPECT_EQ(test.Call("list", "GET", Verified("boss", "boss@example.com"))["payload"]["rooms"].size(), 2u);
}

TEST(NRoomOwnershipTest, OpeningARoomAgainSaysItWasAlreadyThere)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  const json first  = test.OpenBy(Verified("alice", "alice@example.com"), "mine", /*wait=*/true);
  const json second = test.OpenBy(Verified("alice", "alice@example.com"), "mine", /*wait=*/true);
  EXPECT_EQ(first["payload"]["created"], true);
  // room/open is ensure, so the second call succeeds - and says it did not make anything.
  ASSERT_EQ(second["result"], "success");
  EXPECT_EQ(second["payload"]["created"], false);
  EXPECT_EQ(second["payload"]["room"], first["payload"]["room"]);
  EXPECT_EQ(second["payload"]["access"]["rw"], first["payload"]["access"]["rw"]);
  EXPECT_EQ(second["payload"]["owner"], "alice");
}

TEST(NRoomOwnershipTest, AQualifiedIdIsThatRoomWhicheverComesAsking)
{
  Router test(2, {"boss@example.com"});
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};
  test.OpenBy(Verified("alice", "alice@example.com"), "mine", /*wait=*/true);
  const std::string qualified = "alice-mine";

  // Handed on to somebody with no identity at all - which is what a link is for - it opens alice's
  // room, and is not qualified a second time.
  const json anonymous = test.OpenBy(json::object(), qualified, /*wait=*/true);
  ASSERT_EQ(anonymous["result"], "success");
  EXPECT_EQ(anonymous["payload"]["room"], qualified);
  EXPECT_EQ(anonymous["payload"]["owner"], "alice");

  // bob, being somebody else, is refused rather than quietly given a room of his own under her id.
  const json refused = test.OpenBy(Verified("bob", "bob@example.com"), qualified, /*wait=*/true);
  ASSERT_EQ(refused["result"], "failure");
  EXPECT_EQ(refused["code"], "not_owner");
  EXPECT_FALSE(test.router->Tracked("bob-alice-mine"));
}

TEST(NRoomOwnershipTest, ARoomCreatedBeforeOwnershipKeepsItsOwnId)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // A room the cluster already has, with no owner: what a deployment looked like before any of this.
  const std::string services = "/apis/serving.knative.dev/v1/namespaces/default/services";
  json              old;
  old["metadata"]["name"]                      = "ndmspc-room-old";
  old["metadata"]["labels"]["ndmspc.io/room"]  = "old";
  test.cluster->objects[services + "/ndmspc-room-old"] = old;

  // An anonymous caller reaches it under its own id, and nothing is qualified for them.
  // (`wait=false`: the id is the point here, not the room's readiness.)
  const json opened = test.OpenBy(json::object(), "old", /*wait=*/false);
  ASSERT_EQ(opened["result"], "success");
  EXPECT_EQ(opened["payload"]["room"], "old");
  EXPECT_EQ(opened["payload"]["created"], false);
  EXPECT_FALSE(opened["payload"].contains("owner"));

  // An identified caller is told whose it is - nobody's - rather than being refused without a reason.
  const json refused = test.OpenBy(Verified("alice", "alice@example.com"), "old", /*wait=*/true);
  ASSERT_EQ(refused["result"], "failure");
  EXPECT_EQ(refused["code"], "not_owner");
  EXPECT_NE(refused["error"].get<std::string>().find("has no owner"), std::string::npos);
}

TEST(NRoomOwnershipTest, ARestoreKeepsTheIdsTheDocumentNames)
{
  Router test;
  test.cluster->skeleton = {{"serviceSpec", {{"template", {{"spec", {{"containers", json::array()}}}}}}}};

  // A document names its rooms; a restore brings those rooms back rather than new ones of the
  // caller's, or a backup taken before a login existed could never be restored after it.
  json document;
  document["version"] = 1;
  document["rooms"]   = json::array({json::object({{"room", "mine"}, {"owner", "alice@example.com"}})});
  const json restored = test.Call("restore", "POST", Verified("alice", "alice@example.com", document));
  ASSERT_EQ(restored["result"], "success");
  ASSERT_EQ(restored["payload"]["restored"].size(), 1u);
  EXPECT_EQ(restored["payload"]["restored"][0]["room"], "mine");
  EXPECT_TRUE(test.router->Tracked("mine"));
}
