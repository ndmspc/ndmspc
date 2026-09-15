///
/// httpRoom.C — Room router for ndmspc on Knative.
///
/// The router is the always-on entry Service. It creates one Knative Service
/// per room on demand and writes one HTTPRoute per room matching
/// `?<NDMSPC_ROOM_PARAM>=<id>`, pointing straight at that room's revision
/// Service. Steady-state traffic then goes gateway -> room and never touches
/// the router again.
///
/// URL:   /api/room/open      (GET/POST — ensure a room, returns its URL)
///        /api/room/status    (GET      — report a room's state)
///        /api/room/list      (GET      — list tracked rooms)
///        /api/room/close     (DELETE   — delete a room)
///
/// Usage:
///   ndmspc-server start ngnt -m "httpNgntBase.C,httpRoom.C"
///
/// The room id is taken from the request body `{"room": "<id>"}`; /api/room/open
/// must NOT carry the room query parameter, otherwise (once the room's HTTPRoute
/// exists) the gateway would route the call to the room instead of the router.
/// The client then navigates to the returned `url` (i.e. appends `?<param>=<id>`
/// to the page/socket it wants served by the room).
///
/// Configuration (environment):
///   NDMSPC_ROOM_NAMESPACE   namespace for the room objects        (default: default)
///   NDMSPC_ROOM_PREFIX      room resource name prefix             (default: ndmspc-room-)
///   NDMSPC_ROOM_PARAM       query parameter that identifies a room(default: room)
///   NDMSPC_ROOM_SKELETON    skeleton ConfigMap name               (default: ndmspc-room-skeleton)
///   NDMSPC_ROOM_URL_BASE    external base URL for the room links  (default: empty -> "?param=id")
///   NDMSPC_ROOM_IDLE_TTL    idle time before a room is deleted    (default: 1h)
///   NDMSPC_ROOM_READY_TIMEOUT  how long to wait for a room to become Ready (default: 45s)
///   NDMSPC_ROOM_TOKEN_FILE  ServiceAccount token                  (default: in-cluster path)
///   NDMSPC_ROOM_CA_FILE     cluster CA bundle                     (default: in-cluster path)
///
/// The skeleton ConfigMap owns the room shape (image, env, autoscaling,
/// securityContext); this macro only stamps names/revisions. Its
/// `room-skeleton.json` key must contain:
///   { "routeParentRef": {...}, "routeHostname": "...", "serviceSpec": {...} }
///
/// Kubernetes only: loading this macro aborts startup when
/// KUBERNETES_SERVICE_HOST is unset, since the router needs the in-cluster API
/// to create the per-room Knative Services and HTTPRoutes.
///

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ndmspc/http/NGnHttpServer.h>
#include <ndmspc/http/NGnRouteContext.h>
#include <ndmspc/http/NHttpRequest.h>
#include <ndmspc/core/NLogger.h>

// ===========================================================================
//  Configuration
// ===========================================================================

struct NdmspcRoomConfig {
  std::string ns{"default"};
  std::string prefix{"ndmspc-room-"};
  std::string param{"room"};
  std::string skeleton{"ndmspc-room-skeleton"};
  std::string urlBase;
  long        idleTtlSec{3600};
  long        readyTimeoutSec{45};
  std::string apiServer;
  std::string tokenFile;
  std::string caFile;
};

static std::string NdmspcRoomEnv(const char * name, const std::string & fallback)
{
  const char * value = std::getenv(name);
  return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

// Parses "<n>[smhd]" into seconds (e.g. "1h" -> 3600).
static long NdmspcRoomDuration(const std::string & text, long fallback)
{
  if (text.empty()) return fallback;
  char *     end   = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == nullptr || end == text.c_str()) return fallback;
  std::string unit(end);
  if (unit.empty() || unit == "s") return value;
  if (unit == "m") return value * 60;
  if (unit == "h") return value * 3600;
  if (unit == "d") return value * 86400;
  return fallback;
}

static NdmspcRoomConfig & NdmspcRoomCfg()
{
  static NdmspcRoomConfig cfg = []() {
    NdmspcRoomConfig c;
    c.ns              = NdmspcRoomEnv("NDMSPC_ROOM_NAMESPACE", "default");
    c.prefix          = NdmspcRoomEnv("NDMSPC_ROOM_PREFIX", "ndmspc-room-");
    c.param           = NdmspcRoomEnv("NDMSPC_ROOM_PARAM", "room");
    c.skeleton        = NdmspcRoomEnv("NDMSPC_ROOM_SKELETON", "ndmspc-room-skeleton");
    c.urlBase         = NdmspcRoomEnv("NDMSPC_ROOM_URL_BASE", "");
    c.idleTtlSec      = NdmspcRoomDuration(NdmspcRoomEnv("NDMSPC_ROOM_IDLE_TTL", "1h"), 3600);
    c.readyTimeoutSec = NdmspcRoomDuration(NdmspcRoomEnv("NDMSPC_ROOM_READY_TIMEOUT", "45s"), 45);
    const std::string host = NdmspcRoomEnv("KUBERNETES_SERVICE_HOST", "");
    const std::string port = NdmspcRoomEnv("KUBERNETES_SERVICE_PORT", "443");
    c.apiServer            = host.empty() ? std::string() : ("https://" + host + ":" + port);
    c.tokenFile = NdmspcRoomEnv("NDMSPC_ROOM_TOKEN_FILE", "/var/run/secrets/kubernetes.io/serviceaccount/token");
    c.caFile    = NdmspcRoomEnv("NDMSPC_ROOM_CA_FILE", "/var/run/secrets/kubernetes.io/serviceaccount/ca.crt");
    return c;
  }();
  return cfg;
}

// ===========================================================================
//  Room registry
// ===========================================================================

struct NdmspcRoomState {
  std::string name;
  std::string value;
  std::string revision;
  long        lastSeen{0};
};

static std::map<std::string, NdmspcRoomState> gNdmspcRooms;
static std::mutex                             gNdmspcRoomsMutex;

static long NdmspcRoomNow()
{
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

static std::string NdmspcRoomReadFile(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string data = ss.str();
  while (!data.empty() && (data.back() == '\n' || data.back() == '\r')) data.pop_back();
  return data;
}

// ===========================================================================
//  Small helpers
// ===========================================================================

static std::string NdmspcRoomUrlDecode(const std::string & in)
{
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%' && i + 2 < in.size()) {
      const std::string hex = in.substr(i + 1, 2);
      char *            end = nullptr;
      const long        v   = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        out.push_back(static_cast<char>(v));
        i += 2;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

static std::map<std::string, std::string> NdmspcRoomParseQuery(const std::string & query)
{
  std::map<std::string, std::string> params;
  std::stringstream                  ss(query);
  std::string                        pair;
  while (std::getline(ss, pair, '&')) {
    if (pair.empty()) continue;
    const auto eq  = pair.find('=');
    const auto key = (eq == std::string::npos) ? pair : pair.substr(0, eq);
    const auto val = (eq == std::string::npos) ? std::string() : pair.substr(eq + 1);
    params[NdmspcRoomUrlDecode(key)] = NdmspcRoomUrlDecode(val);
  }
  return params;
}

// Maps an arbitrary room id onto a DNS-1123 label (lowercase alphanumerics and
// dashes). Falls back to a stable hash when nothing usable is left.
static std::string NdmspcRoomSlug(const std::string & value, size_t maxLen)
{
  std::string slug;
  bool        lastDash = false;
  for (char c : value) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      slug.push_back(static_cast<char>(std::tolower(uc)));
      lastDash = false;
    }
    else if (!slug.empty() && !lastDash) {
      slug.push_back('-');
      lastDash = true;
    }
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (slug.size() > maxLen) slug.resize(maxLen);
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (slug.empty()) {
    unsigned long long hash = 1469598103934665603ULL;
    for (char c : value) {
      hash ^= static_cast<unsigned char>(c);
      hash *= 1099511628211ULL;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "r%016llx", hash);
    slug = buf;
    if (slug.size() > maxLen) slug.resize(maxLen);
  }
  return slug;
}

static std::string NdmspcRoomName(const NdmspcRoomConfig & cfg, const std::string & value)
{
  const size_t maxSlug = (cfg.prefix.size() < 63) ? (63 - cfg.prefix.size()) : 1;
  return cfg.prefix + NdmspcRoomSlug(value, maxSlug);
}

// ===========================================================================
//  Kubernetes API access
// ===========================================================================

static Ndmspc::NHttpResponse NdmspcRoomApi(const std::string & method, const std::string & path,
                                           const std::string & body        = "",
                                           const std::string & contentType = "application/json")
{
  const NdmspcRoomConfig &           cfg = NdmspcRoomCfg();
  std::map<std::string, std::string> headers;
  headers["Authorization"] = "Bearer " + NdmspcRoomReadFile(cfg.tokenFile);
  headers["Accept"]        = "application/json";
  headers["Content-Type"]  = contentType;

  // Never let a transport failure escape as an exception: the HTTP handler has
  // no outer catch, so a throw would abort the response and return nothing.
  // status -1 marks "no HTTP response" with the reason in body.
  try {
    Ndmspc::NHttpRequest http;
    return http.request(method, cfg.apiServer + path, body, headers, "", "", "", cfg.caFile, "", false);
  }
  catch (const std::exception & e) {
    Ndmspc::NHttpResponse failure;
    failure.status = -1;
    failure.body   = std::string("transport error: ") + e.what();
    NLogError("[room] %s %s failed: %s", method.c_str(), path.c_str(), e.what());
    return failure;
  }
}

static std::string NdmspcRoomSvcCollection()
{
  return "/apis/serving.knative.dev/v1/namespaces/" + NdmspcRoomCfg().ns + "/services";
}

static std::string NdmspcRoomSvcPath(const std::string & name)
{
  return NdmspcRoomSvcCollection() + "/" + name;
}

static std::string NdmspcRoomRouteCollection()
{
  return "/apis/gateway.networking.k8s.io/v1/namespaces/" + NdmspcRoomCfg().ns + "/httproutes";
}

static std::string NdmspcRoomRoutePath(const std::string & name)
{
  return NdmspcRoomRouteCollection() + "/" + name;
}

static std::string NdmspcRoomRevisionPath(const std::string & name)
{
  return "/apis/serving.knative.dev/v1/namespaces/" + NdmspcRoomCfg().ns + "/revisions/" + name;
}

static std::string NdmspcRoomSkeletonPath()
{
  return "/api/v1/namespaces/" + NdmspcRoomCfg().ns + "/configmaps/" + NdmspcRoomCfg().skeleton;
}

// Reads and parses the skeleton ConfigMap ("room-skeleton.json" key).
static bool NdmspcRoomSkeleton(json & out, std::string & error)
{
  const auto response = NdmspcRoomApi("GET", NdmspcRoomSkeletonPath());
  if (response.status != 200) {
    error = "cannot read skeleton ConfigMap '" + NdmspcRoomCfg().skeleton + "' (HTTP " +
            std::to_string(response.status) + ")";
    if (response.status <= 0 && !response.body.empty()) error += ": " + response.body;
    return false;
  }
  try {
    const json        configMap = json::parse(response.body);
    const std::string raw       = configMap["data"].value("room-skeleton.json", "");
    if (raw.empty()) {
      error = "skeleton ConfigMap has no 'room-skeleton.json' key";
      return false;
    }
    out = json::parse(raw);
  }
  catch (const std::exception & e) {
    error = std::string("cannot parse skeleton JSON: ") + e.what();
    return false;
  }
  return true;
}

// Creates the object, or merge-patches its spec when it already exists.
static bool NdmspcRoomApply(const std::string & collection, const std::string & item, const json & object,
                            std::string & error)
{
  const auto current = NdmspcRoomApi("GET", item);
  if (current.status == 200) {
    json patch;
    patch["spec"]       = object.value("spec", json::object());
    const auto response = NdmspcRoomApi("PATCH", item, patch.dump(), "application/merge-patch+json");
    if (response.status < 200 || response.status >= 300) {
      error = "PATCH " + item + " failed (HTTP " + std::to_string(response.status) + "): " + response.body;
      return false;
    }
    return true;
  }
  if (current.status != 404) {
    error = "GET " + item + " failed (HTTP " + std::to_string(current.status) + "): " + current.body;
    return false;
  }
  const auto response = NdmspcRoomApi("POST", collection, object.dump());
  if (response.status < 200 || response.status >= 300) {
    error = "POST " + collection + " failed (HTTP " + std::to_string(response.status) + "): " + response.body;
    return false;
  }
  return true;
}

static json NdmspcRoomServiceObject(const std::string & name, const std::string & value, const json & skeleton)
{
  json service;
  service["apiVersion"]             = "serving.knative.dev/v1";
  service["kind"]                   = "Service";
  service["metadata"]["name"]       = name;
  service["metadata"]["namespace"]  = NdmspcRoomCfg().ns;
  service["metadata"]["labels"]["ndmspc.io/room"] = value;
  service["spec"]                   = skeleton.value("serviceSpec", json::object());

  // Tag the server with the room id so a room can tell which room it is.
  json & containers = service["spec"]["template"]["spec"]["containers"];
  if (!containers.is_array() || containers.empty()) containers = json::array({json::object()});
  json & env = containers[0]["env"];
  if (!env.is_array()) env = json::array();
  bool hasRoom = false;
  for (const auto & item : env) {
    if (item.value("name", "") == "NDMSPC_ROOM") hasRoom = true;
  }
  if (!hasRoom) {
    json roomEnv;
    roomEnv["name"]  = "NDMSPC_ROOM";
    roomEnv["value"] = value;
    env.push_back(roomEnv);
  }
  return service;
}

static json NdmspcRoomRouteObject(const std::string & name, const std::string & value, const std::string & revision,
                                  const json & skeleton)
{
  json route;
  route["apiVersion"]            = "gateway.networking.k8s.io/v1";
  route["kind"]                  = "HTTPRoute";
  route["metadata"]["name"]      = name;
  route["metadata"]["namespace"] = NdmspcRoomCfg().ns;
  route["metadata"]["labels"]["ndmspc.io/room"] = value;

  route["spec"]["parentRefs"] = json::array({skeleton.value("routeParentRef", json::object())});
  if (skeleton.contains("routeHostname") && skeleton["routeHostname"].is_string()) {
    route["spec"]["hostnames"] = json::array({skeleton["routeHostname"].get<std::string>()});
  }

  json param;
  param["name"]  = NdmspcRoomCfg().param;
  param["type"]  = "Exact";
  param["value"] = value;
  json match;
  match["queryParams"] = json::array({param});

  // The activator needs these to route and scale the pinned revision.
  json headerNamespace;
  headerNamespace["name"]  = "Knative-Serving-Namespace";
  headerNamespace["value"] = NdmspcRoomCfg().ns;
  json headerRevision;
  headerRevision["name"]  = "Knative-Serving-Revision";
  headerRevision["value"] = revision;

  json filter;
  filter["type"]                             = "RequestHeaderModifier";
  filter["requestHeaderModifier"]["set"]     = json::array({headerNamespace, headerRevision});

  json backend;
  backend["name"] = revision;
  backend["port"] = 80;

  json rule;
  rule["matches"]     = json::array({match});
  rule["filters"]     = json::array({filter});
  rule["backendRefs"] = json::array({backend});
  route["spec"]["rules"] = json::array({rule});
  return route;
}

static bool NdmspcRoomWaitReady(const std::string & name, std::string & revision, std::string & error)
{
  const long  deadline = NdmspcRoomNow() + NdmspcRoomCfg().readyTimeoutSec;
  std::string lastMessage;
  while (NdmspcRoomNow() < deadline) {
    const auto response = NdmspcRoomApi("GET", NdmspcRoomSvcPath(name));
    if (response.status == 200) {
      try {
        const json svc    = json::parse(response.body);
        const json status = svc.value("status", json::object());
        revision          = status.value("latestReadyRevisionName", "");
        if (!revision.empty()) return true;
        for (const auto & condition : status.value("conditions", json::array())) {
          if (condition.value("type", "") == "Ready") lastMessage = condition.value("message", "");
        }
      }
      catch (const std::exception &) {
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  error = "timed out waiting for room " + name + " to become ready";
  if (!lastMessage.empty()) error += ": " + lastMessage;
  return false;
}

static void NdmspcRoomDelete(const std::string & name)
{
  NdmspcRoomApi("DELETE", NdmspcRoomRoutePath(name));
  NdmspcRoomApi("DELETE", NdmspcRoomSvcPath(name));
}

// Adopts rooms that already exist in the cluster into the in-memory registry.
// The registry is process-local, so a router restart (image rollout, new
// revision) would otherwise lose every room it had created and never expire
// them. Rooms carry the ndmspc.io/room label, so they can be listed back; the
// unknown "last seen" is taken as now, giving each adopted room a full TTL from
// this point. Runs once per process.
//
// Note: keep this plain (no std::call_once/once_flag) - the ROOT interpreter
// compiles this macro at load time and failed to materialize the JIT symbols
// with those constructs in place.
static bool       gNdmspcRoomsAdopted = false;
static std::mutex gNdmspcAdoptMutex;

static void NdmspcRoomAdopt()
{
  {
    std::lock_guard<std::mutex> lock(gNdmspcAdoptMutex);
    if (gNdmspcRoomsAdopted) return;
    gNdmspcRoomsAdopted = true;
  }

  const NdmspcRoomConfig & cfg = NdmspcRoomCfg();
  if (cfg.apiServer.empty()) return;

  const auto response =
      NdmspcRoomApi("GET", NdmspcRoomSvcCollection() + "?labelSelector=ndmspc.io%2Froom");
  if (response.status != 200) {
    NLogWarning("[room] cannot adopt existing rooms (HTTP %d)", response.status);
    return;
  }
  try {
    const json items   = json::parse(response.body).value("items", json::array());
    size_t     adopted = 0;
    {
      std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
      for (const auto & item : items) {
        const json        metadata = item.value("metadata", json::object());
        const std::string name     = metadata.value("name", "");
        if (name.empty()) continue;
        NdmspcRoomState & state = gNdmspcRooms[name];
        state.name              = name;
        if (state.value.empty()) {
          state.value = metadata.value("labels", json::object()).value("ndmspc.io/room", "");
        }
        if (state.revision.empty()) {
          state.revision = item.value("status", json::object()).value("latestReadyRevisionName", "");
        }
        if (state.lastSeen == 0) state.lastSeen = NdmspcRoomNow();
        ++adopted;
      }
    }
    if (adopted > 0) NLogInfo("[room] adopted %zu existing room(s)", adopted);
  }
  catch (const std::exception & e) {
    NLogWarning("[room] cannot parse existing rooms: %s", e.what());
  }
}

// Deletes rooms that have not been touched within NDMSPC_ROOM_IDLE_TTL.
static void NdmspcRoomSweep()
{
  const NdmspcRoomConfig & cfg = NdmspcRoomCfg();
  if (cfg.idleTtlSec <= 0) return;

  const long                   cutoff = NdmspcRoomNow() - cfg.idleTtlSec;
  std::vector<NdmspcRoomState> expired;
  {
    std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
    for (auto it = gNdmspcRooms.begin(); it != gNdmspcRooms.end();) {
      if (it->second.lastSeen < cutoff) {
        expired.push_back(it->second);
        it = gNdmspcRooms.erase(it);
      }
      else {
        ++it;
      }
    }
  }
  for (const auto & room : expired) {
    NLogInfo("[room] expiring idle room %s (idle > %lds)", room.name.c_str(), cfg.idleTtlSec);
    NdmspcRoomDelete(room.name);
  }
}

static bool NdmspcRoomEnsure(const std::string & value, json & payload, std::string & error)
{
  const NdmspcRoomConfig & cfg = NdmspcRoomCfg();
  if (cfg.apiServer.empty()) {
    error = "not running inside a cluster (KUBERNETES_SERVICE_HOST is unset)";
    return false;
  }

  const std::string name = NdmspcRoomName(cfg, value);

  json skeleton;
  if (!NdmspcRoomSkeleton(skeleton, error)) return false;

  const json service = NdmspcRoomServiceObject(name, value, skeleton);
  if (!NdmspcRoomApply(NdmspcRoomSvcCollection(), NdmspcRoomSvcPath(name), service, error)) return false;

  std::string revision;
  if (!NdmspcRoomWaitReady(name, revision, error)) return false;

  const json route = NdmspcRoomRouteObject(name, value, revision, skeleton);
  if (!NdmspcRoomApply(NdmspcRoomRouteCollection(), NdmspcRoomRoutePath(name), route, error)) return false;

  {
    std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
    NdmspcRoomState &           state = gNdmspcRooms[name];
    state.name                        = name;
    state.value                       = value;
    state.revision                    = revision;
    state.lastSeen                    = NdmspcRoomNow();
  }

  payload["room"]     = value;
  payload["name"]     = name;
  payload["revision"] = revision;
  payload["param"]    = cfg.param;
  payload["url"]      = cfg.urlBase.empty() ? ("?" + cfg.param + "=" + value)
                                            : (cfg.urlBase + "/?" + cfg.param + "=" + value);
  payload["ttl"]      = cfg.idleTtlSec;
  return true;
}

static bool NdmspcRoomClose(const std::string & value, std::string & error)
{
  const std::string name = NdmspcRoomName(NdmspcRoomCfg(), value);
  NdmspcRoomDelete(name);
  {
    std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
    gNdmspcRooms.erase(name);
  }
  return true;
}

static std::string NdmspcRoomRequestId(json & in)
{
  if (in.contains("room") && in["room"].is_string()) return in["room"].get<std::string>();
  if (in.contains("_query") && in["_query"].is_string()) {
    const auto params = NdmspcRoomParseQuery(in["_query"].get<std::string>());
    const auto it     = params.find(NdmspcRoomCfg().param);
    if (it != params.end()) return it->second;
    const auto legacy = params.find("room");
    if (legacy != params.end()) return legacy->second;
  }
  return {};
}

static bool NdmspcRoomTouch(const std::string & name)
{
  std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
  const auto                  it = gNdmspcRooms.find(name);
  if (it == gNdmspcRooms.end()) return false;
  it->second.lastSeen = NdmspcRoomNow();
  return true;
}

// ===========================================================================
//  Handlers
// ===========================================================================

void httpRoom()
{
  // Rooms are a Kubernetes feature: the router creates Knative Services and
  // HTTPRoutes through the in-cluster API. Fail fast when that clearly cannot
  // work, instead of starting a server whose /api/room/* calls would all fail.
  const char * kubernetesHost = std::getenv("KUBERNETES_SERVICE_HOST");
  if (kubernetesHost == nullptr || *kubernetesHost == '\0') {
    NLogError("[room] rooms are supported only inside Kubernetes "
              "(KUBERNETES_SERVICE_HOST is not set). Start without rooms "
              "(--rooms false / NDMSPC_ROOMS unset) or run in a cluster.");
    std::exit(1);
  }

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  Ndmspc::RegisterMcpTool("room/open", {
      .description = "Ensure a room exists (one Knative Service per room) and return the URL that "
                     "serves it. POST/GET with 'room' in the body.",
      .methods     = {"GET", "POST"},
      .inputSchema = {{"properties",
                       {{"room", {{"type", "string"}, {"description", "Room id (any client-chosen string)."}}}}}},
  });
  Ndmspc::RegisterMcpTool("room/status", {
      .description = "Report whether a room is known to the router and its current revision.",
      .methods     = {"GET"},
      .inputSchema = {{"properties", {{"room", {{"type", "string"}}}}}},
  });
  Ndmspc::RegisterMcpTool("room/list", {
      .description = "List the rooms the router is currently tracking (with their last-seen time).",
      .methods     = {"GET"},
  });
  Ndmspc::RegisterMcpTool("room/close", {
      .description = "Delete a room's HTTPRoute and Knative Service immediately.",
      .methods     = {"DELETE"},
      .inputSchema = {{"properties", {{"room", {{"type", "string"}}}}}},
  });

  // -------------------------------------------------------------------------
  //  /api/room/open — ensure a room and hand back its URL
  // -------------------------------------------------------------------------
  handlers["room/open"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NdmspcRoomAdopt();
    NdmspcRoomSweep();

    if (!(method.find("GET") != std::string::npos || method.find("POST") != std::string::npos)) {
      out["result"] = "failure";
      out["error"]  = "Unsupported HTTP method for room/open";
      return;
    }

    const std::string id = NdmspcRoomRequestId(in);
    if (id.empty()) {
      out["result"] = "failure";
      out["error"]  = "Missing room id (send it in the body as {\"room\": \"<id>\"})";
      return;
    }

    json        payload;
    std::string error;
    if (!NdmspcRoomEnsure(id, payload, error)) {
      NLogError("[room] open failed for '%s': %s", id.c_str(), error.c_str());
      out["result"] = "failure";
      out["error"]  = error;
      return;
    }
    NLogInfo("[room] room '%s' ready (%s)", id.c_str(), payload.value("revision", "").c_str());
    out["result"]  = "success";
    out["payload"] = payload;
  };

  // -------------------------------------------------------------------------
  //  /api/room/status — report a room's state
  // -------------------------------------------------------------------------
  handlers["room/status"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                               std::map<std::string, TObject *> &) {
    if (method.find("GET") == std::string::npos) {
      out["result"] = "failure";
      out["error"]  = "Unsupported HTTP method for room/status";
      return;
    }

    const std::string id = NdmspcRoomRequestId(in);
    if (id.empty()) {
      out["result"] = "failure";
      out["error"]  = "Missing room id";
      return;
    }

    const std::string name = NdmspcRoomName(NdmspcRoomCfg(), id);
    NdmspcRoomTouch(name);

    out["result"]       = "success";
    out["payload"]["room"]     = id;
    out["payload"]["name"]     = name;
    out["payload"]["param"]    = NdmspcRoomCfg().param;

    {
      std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
      const auto                  it = gNdmspcRooms.find(name);
      out["payload"]["tracked"]      = (it != gNdmspcRooms.end());
      if (it != gNdmspcRooms.end()) {
        out["payload"]["revision"] = it->second.revision;
        out["payload"]["lastSeen"] = it->second.lastSeen;
      }
    }

    const auto response = NdmspcRoomApi("GET", NdmspcRoomSvcPath(name));
    out["payload"]["exists"] = (response.status == 200);
    if (response.status == 200) {
      try {
        const json svc        = json::parse(response.body);
        const json status     = svc.value("status", json::object());
        bool       ready      = false;
        for (const auto & condition : status.value("conditions", json::array())) {
          if (condition.value("type", "") == "Ready") ready = (condition.value("status", "") == "True");
        }
        out["payload"]["ready"]    = ready;
        out["payload"]["revision"] = status.value("latestReadyRevisionName", "");
      }
      catch (const std::exception &) {
      }
    }
  };

  // -------------------------------------------------------------------------
  //  /api/room/list — rooms the router is tracking
  // -------------------------------------------------------------------------
  handlers["room/list"] = [](std::string method, json & /*in*/, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    if (method.find("GET") == std::string::npos) {
      out["result"] = "failure";
      out["error"]  = "Unsupported HTTP method for room/list";
      return;
    }

    json                     rooms = json::array();
    std::vector<std::string> stale;
    {
      std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
      for (const auto & entry : gNdmspcRooms) {
        json room;
        room["name"]     = entry.second.name;
        room["room"]     = entry.second.value;
        room["revision"] = entry.second.revision;
        room["lastSeen"] = entry.second.lastSeen;
        rooms.push_back(room);
      }
    }

    // Report live state, and drop rooms whose Service has gone away (deleted out
    // of band). A room keeps its Service when idle but scales to zero pods
    // (min-scale 0), so "replicas: 0 / active: false" is the normal resting state
    // - worth showing explicitly rather than leaving the caller guessing.
    for (auto & room : rooms) {
      const std::string name     = room.value("name", "");
      const auto        response = NdmspcRoomApi("GET", NdmspcRoomSvcPath(name));
      if (response.status == 404) {
        stale.push_back(name);
        continue;
      }
      if (response.status != 200) continue;
      try {
        const json svc    = json::parse(response.body);
        const json status = svc.value("status", json::object());
        bool       ready  = false;
        for (const auto & condition : status.value("conditions", json::array())) {
          if (condition.value("type", "") == "Ready") ready = (condition.value("status", "") == "True");
        }
        room["ready"]    = ready;
        room["revision"] = status.value("latestReadyRevisionName", room.value("revision", ""));

        const std::string revision = room.value("revision", "");
        if (!revision.empty()) {
          const auto rev = NdmspcRoomApi("GET", NdmspcRoomRevisionPath(revision));
          if (rev.status == 200) {
            const int replicas = json::parse(rev.body)
                                     .value("status", json::object())
                                     .value("actualReplicas", 0);
            room["replicas"] = replicas;
            room["active"]   = replicas > 0;
          }
        }
      }
      catch (const std::exception &) {
      }
    }
    for (const auto & name : stale) {
      NLogInfo("[room] dropping %s: Service no longer exists", name.c_str());
      {
        std::lock_guard<std::mutex> lock(gNdmspcRoomsMutex);
        gNdmspcRooms.erase(name);
      }
      NdmspcRoomApi("DELETE", NdmspcRoomRoutePath(name));
    }
    if (!stale.empty()) {
      // Also drop them from this response: reconciling on read should not report
      // a room it just found gone.
      json live = json::array();
      for (auto & room : rooms) {
        if (std::find(stale.begin(), stale.end(), room.value("name", "")) == stale.end()) live.push_back(room);
      }
      rooms = live;
    }

    out["result"]           = "success";
    out["payload"]["rooms"] = rooms;
    out["payload"]["ttl"]   = NdmspcRoomCfg().idleTtlSec;
  };

  // -------------------------------------------------------------------------
  //  /api/room/close — delete a room
  // -------------------------------------------------------------------------
  handlers["room/close"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                              std::map<std::string, TObject *> &) {
    if (method.find("DELETE") == std::string::npos) {
      out["result"] = "failure";
      out["error"]  = "Unsupported HTTP method for room/close";
      return;
    }

    const std::string id = NdmspcRoomRequestId(in);
    if (id.empty()) {
      out["result"] = "failure";
      out["error"]  = "Missing room id";
      return;
    }

    std::string error;
    if (!NdmspcRoomClose(id, error)) {
      out["result"] = "failure";
      out["error"]  = error;
      return;
    }
    out["result"]  = "success";
    out["payload"]["room"] = id;
    out["payload"]["name"] = NdmspcRoomName(NdmspcRoomCfg(), id);
  };
}
