// The room router: the framework capability behind `ndmspc-server --rooms true`.
//
// NRoomRouter.h says what it is and what it guarantees; this file is its implementation. The
// helpers below stay file-local - the router is one object per process, reached through
// NRoomRouter::Instance() by the actions it registers into gNdmspcHttpHandlers.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NRouteContext.h"
#include "ndmspc/http/NHttpRequest.h"
#include "ndmspc/http/NRoomSession.h"
#include "ndmspc/core/NLogger.h"

#include "NRoomRouter.h"

namespace Ndmspc {

/**
 * @brief The label every object the router creates carries.
 *
 * It is how rooms are read back (Adopt) and the router's ownership test: only objects carrying it
 * are ever patched or deleted, so a room name that something else already holds (the router's own
 * entry Service above all) is reported as a conflict instead of being overwritten.
 */
static const char kNRoomLabel[] = "ndmspc.io/room";

// ===========================================================================
//  Kubernetes API access
// ===========================================================================

NHttpResponse NRoomRouter::Request(const std::string & method, const std::string & path, const std::string & body,
                                   const std::string & contentType)
{
  return fCluster->Request(method, path, body, contentType);
}

// ===========================================================================
//  Configuration
// ===========================================================================

// ===========================================================================
//  Configuration
// ===========================================================================

/**
 * @brief Reads an environment variable, falling back when it is unset or empty.
 * @param name Variable name.
 * @param fallback Value returned when the variable is unset or empty.
 * @return The variable's value, or fallback.
 */
static std::string NRoomEnv(const char * name, const std::string & fallback)
{
  const char * value = std::getenv(name);
  return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

/**
 * @brief Splits a comma-separated list, dropping empty entries and surrounding space.
 * @param text The list, e.g. "alice@example.com, bob".
 * @return The entries, in the order they were written.
 */
static std::vector<std::string> NRoomList(const std::string & text)
{
  std::vector<std::string> items;
  std::string              current;
  for (char c : text + ",") {
    if (c != ',') {
      current.push_back(c);
      continue;
    }
    const auto first = current.find_first_not_of(" \t");
    const auto last  = current.find_last_not_of(" \t");
    if (first != std::string::npos) items.push_back(current.substr(first, last - first + 1));
    current.clear();
  }
  return items;
}

// Parses "1/0", "true/false", "yes/no" and "on/off"; anything else keeps the fallback.
bool NRoomConfig::ParseBool(const std::string & text, bool fallback)
{
  std::string value;
  value.reserve(text.size());
  for (char c : text) value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
  if (value == "0" || value == "false" || value == "no" || value == "off") return false;
  return fallback;
}

// Parses "<n>[smhd]" into seconds (e.g. "1h" -> 3600).
long NRoomConfig::ParseDuration(const std::string & text, long fallback)
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

// Every knob the router reads, and the Kubernetes environment it needs to serve rooms.
NRoomConfig NRoomConfig::FromEnv()
{
  NRoomConfig c;
  c.ns              = NRoomEnv("NDMSPC_ROOM_NAMESPACE", "default");
  c.prefix          = NRoomEnv("NDMSPC_ROOM_PREFIX", "ndmspc-room-");
  c.param           = NRoomEnv("NDMSPC_ROOM_PARAM", "room");
  c.skeleton        = NRoomEnv("NDMSPC_ROOM_SKELETON", "ndmspc-room-skeleton");
  c.urlBase         = NRoomEnv("NDMSPC_ROOM_URL_BASE", "");
  c.idleTtlSec      = ParseDuration(NRoomEnv("NDMSPC_ROOM_IDLE_TTL", "1h"), 3600);
  c.readyTimeoutSec = ParseDuration(NRoomEnv("NDMSPC_ROOM_READY_TIMEOUT", "45s"), 45);
  c.maxPreparing    = static_cast<int>(std::strtol(NRoomEnv("NDMSPC_ROOM_MAX_PREPARING", "4").c_str(), nullptr, 10));
  c.waitDefault     = ParseBool(NRoomEnv("NDMSPC_ROOM_WAIT", "true"), true);
  c.admins          = NRoomList(NRoomEnv("NDMSPC_ROOM_ADMINS", ""));
  const std::string host = NRoomEnv("KUBERNETES_SERVICE_HOST", "");
  const std::string port = NRoomEnv("KUBERNETES_SERVICE_PORT", "443");
  c.apiServer            = host.empty() ? std::string() : ("https://" + host + ":" + port);
  c.tokenFile = NRoomEnv("NDMSPC_ROOM_TOKEN_FILE", "/var/run/secrets/kubernetes.io/serviceaccount/token");
  c.caFile    = NRoomEnv("NDMSPC_ROOM_CA_FILE", "/var/run/secrets/kubernetes.io/serviceaccount/ca.crt");
  return c;
}

// The router's configuration: read once, from the environment.
// ===========================================================================
//  Room registry
// ===========================================================================


/// @brief The current time in epoch seconds (how every room time is recorded).
static long NdmspcRoomNow()
{
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

/**
 * @brief Reads an integer member of a JSON object, falling back when it is absent or not an integer.
 * @param object Object to read from.
 * @param key Member name.
 * @param fallback Value returned when the member is absent or not an integer.
 * @return The member's value, or fallback.
 */
static int NdmspcRoomInt(const json & object, const char * key, int fallback = 0)
{
  if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer()) return fallback;
  return object[key].get<int>();
}

/**
 * @brief Reads a member of a JSON object, falling back when it is absent.
 * @param object Object to read from.
 * @param key Member name.
 * @param fallback Value returned when the member is absent.
 * @return The member's value, or fallback.
 */
static json NdmspcRoomMember(const json & object, const char * key, const json & fallback = json())
{
  if (!object.is_object() || !object.contains(key)) return fallback;
  return object[key];
}

// ===========================================================================
//  Small helpers
// ===========================================================================

std::string NRoomRouter::UrlDecode(const std::string & in)
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

std::map<std::string, std::string> NRoomRouter::ParseQuery(const std::string & query)
{
  std::map<std::string, std::string> params;
  std::stringstream                  ss(query);
  std::string                        pair;
  while (std::getline(ss, pair, '&')) {
    if (pair.empty()) continue;
    const auto eq  = pair.find('=');
    const auto key = (eq == std::string::npos) ? pair : pair.substr(0, eq);
    const auto val = (eq == std::string::npos) ? std::string() : pair.substr(eq + 1);
    params[NRoomRouter::UrlDecode(key)] = NRoomRouter::UrlDecode(val);
  }
  return params;
}

// Maps an arbitrary room id onto a DNS-1123 label (lowercase alphanumerics and
// dashes). Falls back to a stable hash when nothing usable is left.
std::string NRoomRouter::Slug(const std::string & value, size_t maxLen)
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

std::string NRoomRouter::RoomName(const NRoomConfig & cfg, const std::string & value)
{
  const size_t maxSlug = (cfg.prefix.size() < 63) ? (63 - cfg.prefix.size()) : 1;
  return cfg.prefix + NRoomRouter::Slug(value, maxSlug);
}

bool NRoomRouter::HasRoomLabel(const json & object)
{
  if (!object.is_object()) return false;
  const json labels = object.value("metadata", json::object()).value("labels", json::object());
  return labels.is_object() && labels.contains(kNRoomLabel);
}

// The URL a client is handed for a room: the external base, the room parameter and, when it has
// one, the access token that lets it in.
std::string NRoomRouter::ClientUrl(const NRoomConfig & cfg, const std::string & value, const std::string & token)
{
  std::string url =
      cfg.urlBase.empty() ? ("?" + cfg.param + "=" + value) : (cfg.urlBase + "/?" + cfg.param + "=" + value);
  if (!token.empty()) url += "&" + std::string(kAccessParam) + "=" + token;
  return url;
}

// A fresh room access token: 128 bits of entropy, hex, short enough to sit in a link. A room is the
// only thing it guards and lives for hours, so there is no rotation and no secret store.
std::string NRoomRouter::NewRoomToken()
{
  static const char digits[] = "0123456789abcdef";
  std::random_device random;
  std::string        token(32, '0');
  for (auto & character : token) character = digits[random() & 0x0f];
  return token;
}

// The tokens as they travel: into the room through its environment, back to clients in payloads,
// and onto the room's own Service so that a router restart does not lose them.
json NRoomRouter::AccessJson(const std::string & tokenRw, const std::string & tokenRo)
{
  json access = json::object();
  if (!tokenRw.empty()) access["rw"] = tokenRw;
  if (!tokenRo.empty()) access["ro"] = tokenRo;
  return access;
}

// A room's tokens, read under the registry lock.
json NRoomRouter::RoomAccess(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it == fRooms.end()) return json::object();
  return NRoomRouter::AccessJson(it->second.tokenRw, it->second.tokenRo);
}

// A room's owner, read under the registry lock.
std::string NRoomRouter::RoomOwner(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  return it == fRooms.end() ? std::string() : it->second.owner;
}

// Whether a caller is one of the configured admins. The list may hold emails or user names, and a
// caller is matched on every identifier they are known by, so a token whose user name differs from
// its email still matches an entry naming either.
bool NRoomRouter::IsAdmin(const NRequestIdentity & identity) const
{
  if (identity.Empty()) return false;
  for (const auto & admin : fConfig.admins) {
    if (identity.Matches(admin)) return true;
  }
  return false;
}

// Whether a caller may see and act on a room: see "Ownership and visibility" in the class note.
bool NRoomRouter::MaySee(const NRoomState & room, const NRequestIdentity & identity) const
{
  // A request that says nothing about its caller is an operator's script or the room TUI, which the
  // router has always answered with every room.
  if (identity.Empty()) return true;
  if (IsAdmin(identity)) return true;
  // A room with no owner belongs to nobody, and Matches("") is false for everyone, so nobody but an
  // admin or an anonymous caller is shown it.
  return identity.Matches(room.owner);
}

// The owner a client asserts, used only when nothing verified the request (see RequestIdentity).
std::string NRoomRouter::RequestOwner(json & in)
{
  const json owner = NdmspcRoomMember(in, "owner");
  if (owner.is_string() && !owner.get<std::string>().empty()) return owner.get<std::string>();
  const json query = NdmspcRoomMember(in, "_query");
  if (query.is_string()) {
    const auto params = NRoomRouter::ParseQuery(query.get<std::string>());
    const auto it     = params.find("owner");
    if (it != params.end() && !it->second.empty()) return it->second;
  }
  return {};
}

// The caller of a request: what the server verified wins, and the client's own word is the fallback.
NRequestIdentity NRoomRouter::RequestIdentity(json & in)
{
  const NRequestIdentity identity = NRequestIdentity::FromJson(NdmspcRoomMember(in, "_identity"));
  if (identity.verified) return identity;
  const std::string asserted = NRoomRouter::RequestOwner(in);
  return asserted.empty() ? identity : NRequestIdentity::FromAssertion(asserted);
}


std::string NRoomRouter::SvcCollection() const
{
  return "/apis/serving.knative.dev/v1/namespaces/" + fConfig.ns + "/services";
}

std::string NRoomRouter::SvcPath(const std::string & name) const
{
  return SvcCollection() + "/" + name;
}

std::string NRoomRouter::RouteCollection() const
{
  return "/apis/gateway.networking.k8s.io/v1/namespaces/" + fConfig.ns + "/httproutes";
}

std::string NRoomRouter::RoutePath(const std::string & name) const
{
  return RouteCollection() + "/" + name;
}

std::string NRoomRouter::RevisionPath(const std::string & name) const
{
  return "/apis/serving.knative.dev/v1/namespaces/" + fConfig.ns + "/revisions/" + name;
}

std::string NRoomRouter::SkeletonPath() const
{
  return "/api/v1/namespaces/" + fConfig.ns + "/configmaps/" + fConfig.skeleton;
}

/**
 * @brief The Service annotation holding a room's last session.
 *
 * This is what lets an idle (scaled to zero) room be brought back with the file, navigator and
 * drill-down it had. It is kept on the room's own Knative Service: annotating a Service does not
 * create a revision, so this cannot disturb a running room, and the snapshot survives a router
 * restart because Adopt reads it back.
 */
static const char kNRoomStateAnnotation[] = "ndmspc.io/room-state";

// The address that serves a room directly, in-cluster. Knative creates one Service per
// revision, named after it - which is exactly what the room's HTTPRoute targets - so this
// reaches the room without going through the gateway (and scales it up on the way).
std::string NRoomRouter::RoomBaseUrl(const std::string & revision) const
{
  if (revision.empty()) return {};
  return "http://" + revision + "." + fConfig.ns + ".svc.cluster.local:80";
}

// The router's own address as seen from a room, which a room uses to report its session and
// to fetch it back when it wakes. Knative exports the Service name as K_SERVICE, so the
// router does not need to be told its own name.
std::string NRoomRouter::RouterBaseUrl() const
{
  const std::string service = NRoomEnv("K_SERVICE", "");
  if (service.empty()) return {};
  return "http://" + service + "." + fConfig.ns + ".svc.cluster.local:80";
}

// Reads and parses the skeleton ConfigMap ("room-skeleton.json" key).
bool NRoomRouter::Skeleton(json & out, std::string & error)
{
  const auto response = Request("GET", SkeletonPath());
  if (response.status != 200) {
    error = "cannot read skeleton ConfigMap '" + fConfig.skeleton + "' (HTTP " +
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
// An object that already holds the name but is not one of ours is never touched: the room fails
// with name_conflict instead, so a name that happens to be taken (the router's own Service above
// all) cannot be overwritten.
bool NRoomRouter::Apply(const std::string & collection, const std::string & item, const json & object,
                        std::string & error, std::string & code)
{
  code.clear();
  const auto current = Request("GET", item);
  if (current.status == 200) {
    json existing;
    try {
      existing = json::parse(current.body);
    }
    catch (const std::exception &) {
      error = "GET " + item + " returned a body that is not JSON";
      return false;
    }
    if (!NRoomRouter::HasRoomLabel(existing)) {
      error = "refusing to modify " + item + ": it is not a room (no " + std::string(kNRoomLabel) +
              " label). Pick another room id, or remove whatever owns that name";
      code  = kNameConflict;
      return false;
    }
    json patch;
    patch["spec"]       = object.value("spec", json::object());
    const auto response = Request("PATCH", item, patch.dump(), "application/merge-patch+json");
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
  const auto response = Request("POST", collection, object.dump());
  if (response.status < 200 || response.status >= 300) {
    error = "POST " + collection + " failed (HTTP " + std::to_string(response.status) + "): " + response.body;
    return false;
  }
  return true;
}

// Stores a room's session snapshot on its own Knative Service.
bool NRoomRouter::Annotate(const std::string & name, const std::string & snapshot, std::string & error)
{
  json patch;
  patch["metadata"]["annotations"][kNRoomStateAnnotation] = snapshot;

  const auto response =
      Request("PATCH", SvcPath(name), patch.dump(), "application/merge-patch+json");
  if (response.status < 200 || response.status >= 300) {
    error = "PATCH " + SvcPath(name) + " failed (HTTP " + std::to_string(response.status) +
            "): " + response.body;
    return false;
  }
  return true;
}

// Returns a room's stored session snapshot.
//
// The registry is process-local, so a router restart (or a scale-from-zero) loses it while the
// snapshot itself survives on the room's own Service - read it back from there rather than
// telling a room that has one that it has none.
std::string NRoomRouter::Snapshot(const std::string & name, const std::string & id)
{
  std::string text;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it != fRooms.end()) text = it->second.snapshot;
  }
  if (!text.empty()) return text;

  const auto response = Request("GET", SvcPath(name));
  if (response.status == 200) {
    try {
      const json svc         = json::parse(response.body);
      const json annotations = svc.value("metadata", json::object()).value("annotations", json::object());
      text                   = annotations.value(kNRoomStateAnnotation, std::string());
    }
    catch (const std::exception &) {
    }
  }
  if (text.empty()) return text;

  {
    std::lock_guard<std::mutex> lock(fMutex);
    NRoomState &           state = fRooms[name];
    state.name                        = name;
    state.value                       = id;
    state.snapshot                    = text;
    if (state.lastSeen == 0) state.lastSeen = NdmspcRoomNow();
  }
  NLogInfo("[room] %s: read its stored session back from the Service", name.c_str());
  return text;
}

// Remembers a room's session: in the registry, and on the room's Service so it outlives this
// process. Annotating cannot disturb the room (it does not change the spec, so no new revision).
void NRoomRouter::StoreSnapshot(const std::string & name, const std::string & id, const std::string & text)
{
  {
    std::lock_guard<std::mutex> lock(fMutex);
    NRoomState &           state = fRooms[name];
    state.name                        = name;
    state.value                       = id;
    state.snapshot                    = text;
    if (state.lastSeen == 0) state.lastSeen = NdmspcRoomNow();
  }

  std::string error;
  if (!Annotate(name, text, error)) {
    NLogWarning("[room] cannot store the session of %s: %s", name.c_str(), error.c_str());
  }
}

json NRoomRouter::ServiceObject(const NRoomConfig & cfg, const std::string & name, const std::string & value,
                                const std::string & stateUrl, const json & access, const std::string & owner,
                                const json & skeleton)
{
  json service;
  service["apiVersion"]                      = "serving.knative.dev/v1";
  service["kind"]                            = "Service";
  service["metadata"]["name"]                = name;
  service["metadata"]["namespace"]           = cfg.ns;
  service["metadata"]["labels"][kNRoomLabel] = value;
  service["spec"]                            = skeleton.value("serviceSpec", json::object());

  // Keep the room's access tokens on the Service itself: a room scales to zero and the router can
  // restart, and both have to come back knowing which links still open this room. Annotating the
  // metadata does not create a revision, so a running room is not disturbed.
  if (!access.is_null() && !access.empty()) {
    service["metadata"]["annotations"][kAccessAnnotation] = access.dump();
  }

  // Its owner is kept there for the same reason: whoever created the room stays its owner across a
  // restart, an idle room waking up, and a restore.
  if (!owner.empty()) {
    service["metadata"]["annotations"][kOwnerAnnotation] = owner;
  }

  // Tag the server with the room id so a room can tell which room it is, tell it where to report
  // its session, and hand it the tokens its own traffic has to carry. A value the skeleton
  // already sets wins.
  json & containers = service["spec"]["template"]["spec"]["containers"];
  if (!containers.is_array() || containers.empty()) containers = json::array({json::object()});
  json & env = containers[0]["env"];
  if (!env.is_array()) env = json::array();

  auto ensureEnv = [&env](const std::string & key, const std::string & value) {
    if (value.empty()) return;
    for (const auto & item : env) {
      if (item.value("name", "") == key) return;
    }
    json entry;
    entry["name"]  = key;
    entry["value"] = value;
    env.push_back(entry);
  };
  ensureEnv("NDMSPC_ROOM", value);
  ensureEnv("NDMSPC_ROOM_STATE_URL", stateUrl);
  if (!access.is_null() && !access.empty()) ensureEnv(kAccessEnv, access.dump());

  return service;
}

json NRoomRouter::RouteObject(const NRoomConfig & cfg, const std::string & name, const std::string & value,
                              const std::string & revision, const json & skeleton)
{
  json route;
  route["apiVersion"]                      = "gateway.networking.k8s.io/v1";
  route["kind"]                            = "HTTPRoute";
  route["metadata"]["name"]                = name;
  route["metadata"]["namespace"]           = cfg.ns;
  route["metadata"]["labels"][kNRoomLabel] = value;

  route["spec"]["parentRefs"] = json::array({skeleton.value("routeParentRef", json::object())});
  if (skeleton.contains("routeHostname") && skeleton["routeHostname"].is_string()) {
    route["spec"]["hostnames"] = json::array({skeleton["routeHostname"].get<std::string>()});
  }

  json param;
  param["name"]  = cfg.param;
  param["type"]  = "Exact";
  param["value"] = value;
  json match;
  match["queryParams"] = json::array({param});

  // The activator needs these to route and scale the pinned revision.
  json headerNamespace;
  headerNamespace["name"]  = "Knative-Serving-Namespace";
  headerNamespace["value"] = cfg.ns;
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

// The scheduler's own words for a pod it cannot place.
//
// A Knative Service only ever reports "waiting for a Revision to become ready"; the reason lives on
// the pod ("0/1 nodes are available: 1 Insufficient cpu"), so that is where this looks. Returns
// kTRUE when a pod of that revision is reported unschedulable, with the message in reason.
//
// This needs read access to pods. When the cluster refuses (403) or answers anything else, it
// simply reports no reason and the caller keeps its previous behaviour - a missing permission must
// not fail a room.
bool NRoomRouter::PodUnschedulable(const std::string & name, const std::string & revision, std::string & reason)
{
  const NRoomConfig & cfg = fConfig;

  // Knative labels each pod with its revision; before one exists, fall back to the room's service.
  const std::string selector = revision.empty() ? ("serving.knative.dev%2Fservice%3D" + name)
                                                : ("serving.knative.dev%2Frevision%3D" + revision);

  const auto response = Request("GET", "/api/v1/namespaces/" + cfg.ns + "/pods?labelSelector=" + selector);
  if (response.status != 200) return false;

  try {
    const json items = json::parse(response.body).value("items", json::array());
    for (const auto & pod : items) {
      const json conditions = pod.value("status", json::object()).value("conditions", json::array());
      for (const auto & condition : conditions) {
        if (condition.value("type", "") != "PodScheduled" || condition.value("status", "") != "False") continue;
        if (condition.value("reason", "") != "Unschedulable") continue;
        reason = condition.value("message", "the cluster cannot place the room's pod");
        return true;
      }
    }
  }
  catch (const std::exception &) {
  }
  return false;
}

bool NRoomRouter::WaitReady(const std::string & name, std::string & revision, std::string & error,
                                std::string & code)
{
  const long  deadline = NdmspcRoomNow() + fConfig.readyTimeoutSec;
  std::string lastMessage;
  std::string lastUnschedulable;   // the scheduler's verdict, and how often in a row it has said it
  int         unschedulableSeen = 0;

  while (NdmspcRoomNow() < deadline) {
    const auto response = Request("GET", SvcPath(name));
    if (response.status == 200) {
      try {
        const json        svc        = json::parse(response.body);
        const json        status     = svc.value("status", json::object());
        const json        metadata   = svc.value("metadata", json::object());
        const std::string created    = status.value("latestCreatedRevisionName", "");
        const std::string ready      = status.value("latestReadyRevisionName", "");
        const int         generation = NdmspcRoomInt(metadata, "generation");
        const int         observed   = NdmspcRoomInt(status, "observedGeneration");

        // The room serves the spec just applied only once the controller has observed that
        // generation and the newest revision is the ready one. Waiting merely for "some ready
        // revision" would pin this room's HTTPRoute - and the ?room= traffic with it - to the
        // previous revision after any spec change (a new image, a new env var), silently
        // leaving the room on the old code. Comparing created with ready is not enough on its
        // own: both still describe the previous spec until the status catches up.
        if (generation > 0 && observed >= generation && !created.empty() && created == ready) {
          revision = ready;
          return true;
        }

        // A pod the scheduler cannot place will never become ready, and the Service will never say
        // why: report the scheduler's own words now instead of waiting out the whole timeout. The
        // verdict has to repeat, so a race while another room scales up cannot fail this one.
        std::string unschedulable;
        if (PodUnschedulable(name, created, unschedulable)) {
          if (unschedulable == lastUnschedulable) {
            ++unschedulableSeen;
          }
          else {
            lastUnschedulable  = unschedulable;
            unschedulableSeen = 1;
          }
          if (unschedulableSeen >= 2) {
            error = NRoomRouter::UnschedulableError(unschedulable);
            code  = NRoomRouter::kNoCapacity;
            return false;
          }
        }
        else {
          lastUnschedulable.clear();
          unschedulableSeen = 0;
        }

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
  if (!lastUnschedulable.empty()) {
    // Give the reason even when the verdict never repeated: "timed out" alone tells nobody anything.
    error += " (its pod is still unschedulable: " + lastUnschedulable + ")";
  }
  else if (!lastMessage.empty()) {
    error += ": " + lastMessage;
  }
  return false;
}

// Deletes a room's Service and HTTPRoute - but only objects that are actually rooms. The router
// must never take out anything it did not create, so whatever holds the name is read back first and
// left alone when it carries no room label.
void NRoomRouter::Delete(const std::string & name)
{
  const auto service = Request("GET", SvcPath(name));
  if (service.status == 200) {
    try {
      if (!NRoomRouter::HasRoomLabel(json::parse(service.body))) {
        NLogWarning("[room] not deleting %s: it is not a room (no %s label)", name.c_str(), kNRoomLabel);
      }
      else {
        Request("DELETE", SvcPath(name));
      }
    }
    catch (const std::exception & e) {
      NLogWarning("[room] not deleting %s: cannot read it back (%s)", name.c_str(), e.what());
    }
  }

  const auto route = Request("GET", RoutePath(name));
  if (route.status == 200) {
    try {
      if (NRoomRouter::HasRoomLabel(json::parse(route.body))) {
        Request("DELETE", RoutePath(name));
      }
    }
    catch (const std::exception & e) {
      NLogWarning("[room] not deleting the route of %s: cannot read it back (%s)", name.c_str(), e.what());
    }
  }
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

void NRoomRouter::Adopt()
{
  {
    std::lock_guard<std::mutex> lock(fAdoptMutex);
    if (fAdopted) return;
    fAdopted = true;
  }

  const NRoomConfig & cfg = fConfig;
  if (cfg.apiServer.empty()) return;

  const auto response =
      Request("GET", SvcCollection() + "?labelSelector=ndmspc.io%2Froom");
  if (response.status != 200) {
    NLogWarning("[room] cannot adopt existing rooms (HTTP %d)", response.status);
    return;
  }
  try {
    const json items   = json::parse(response.body).value("items", json::array());
    size_t     adopted = 0;
    {
      std::lock_guard<std::mutex> lock(fMutex);
      for (const auto & item : items) {
        const json        metadata = item.value("metadata", json::object());
        const std::string name     = metadata.value("name", "");
        if (name.empty()) continue;
        NRoomState & state = fRooms[name];
        state.name              = name;
        if (state.value.empty()) {
          state.value = metadata.value("labels", json::object()).value(kNRoomLabel, "");
        }
        if (state.revision.empty()) {
          state.revision = item.value("status", json::object()).value("latestReadyRevisionName", "");
        }
        if (state.lastSeen == 0) state.lastSeen = NdmspcRoomNow();
        if (state.snapshot.empty()) {
          state.snapshot = metadata.value("annotations", json::object()).value(kNRoomStateAnnotation, "");
        }
        if (state.tokenRw.empty() || state.tokenRo.empty()) {
          // Read the tokens back from the room's own Service. A room that predates them has none,
          // and is left that way: minting here would roll a new revision on a running room.
          const std::string text = metadata.value("annotations", json::object()).value(kAccessAnnotation, "");
          if (!text.empty()) {
            try {
              const json access = json::parse(text);
              if (state.tokenRw.empty()) state.tokenRw = access.value("rw", "");
              if (state.tokenRo.empty()) state.tokenRo = access.value("ro", "");
            }
            catch (const std::exception &) {
            }
          }
        }
        if (state.owner.empty()) {
          // Same for the owner: a room created before ownership existed has none, and stays unowned
          // rather than being handed to whoever asks for it next.
          state.owner = metadata.value("annotations", json::object()).value(kOwnerAnnotation, "");
        }
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
void NRoomRouter::Sweep()
{
  const NRoomConfig & cfg = fConfig;
  if (cfg.idleTtlSec <= 0) return;

  const long                   cutoff = NdmspcRoomNow() - cfg.idleTtlSec;
  std::vector<NRoomState> expired;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    for (auto it = fRooms.begin(); it != fRooms.end();) {
      // A room being created is never expired: it has no session yet, and its creation would be
      // deleted out from under the worker.
      if (it->second.lastSeen < cutoff && !it->second.preparing) {
        expired.push_back(it->second);
        it = fRooms.erase(it);
      }
      else {
        ++it;
      }
    }
  }
  for (const auto & room : expired) {
    NLogInfo("[room] expiring idle room %s (idle > %lds)", room.name.c_str(), cfg.idleTtlSec);
    Delete(room.name);
  }
}

void NRoomRouter::CloseRoom(const std::string & value)
{
  const std::string name = NRoomRouter::RoomName(fConfig, value);

  // Tell a creation that may be running for this room to stop: it checks this between steps and
  // would otherwise pin an HTTPRoute onto a Service that is being deleted right here.
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it != fRooms.end()) it->second.cancel = true;
  }

  Delete(name);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    fRooms.erase(name);
  }
}

// Captures a room's session and remembers it, so it can be replayed when the room is next
// opened. Only ever called for a room that is running: a request to a scaled-to-zero room
// would wake it, which is exactly what min-scale 0 exists to avoid.
//
// This runs on a detached thread. The capture calls the room, and a room that has just woken
// calls the router back (to restore its own session), so doing this inside room/list would put
// the router - which serves one request at a time - in a cycle with the room and starve that
// fetch. Off the request path the router is free to answer.
void NRoomRouter::CaptureNow(const std::string & name, const std::string & value,
                                 const std::string & revision)
{
  const std::string baseUrl = RoomBaseUrl(revision);
  if (baseUrl.empty()) return;

  Ndmspc::NHttpRequest http;
  std::string         error;
  const json          access = RoomAccess(name);
  const json          snapshot =
      Ndmspc::NRoomSession::Capture(http, baseUrl, value, error, access.value(NRoomAccess::kReadWrite, ""));
  if (snapshot.is_null()) {
    // NRoomSession reports nothing for a room that has no file open, so a freshly
    // started pod can never overwrite a good snapshot with emptiness.
    if (!error.empty()) {
      NLogWarning("[room] cannot capture the session of %s: %s", name.c_str(), error.c_str());
    }
    return;
  }

  const std::string text = Ndmspc::NRoomSession::Encode(snapshot);
  if (text.empty()) return;

  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it == fRooms.end()) return;
    if (it->second.snapshot == text) return; // unchanged since the last poll
    it->second.snapshot = text;
  }

  std::string file;
  if (snapshot.contains("file") && snapshot["file"].is_string()) file = snapshot["file"].get<std::string>();
  NLogInfo("[room] captured the session of %s (file '%s')", name.c_str(), file.c_str());

  std::string patchError;
  if (!Annotate(name, text, patchError)) {
    NLogWarning("[room] cannot store the session of %s: %s", name.c_str(), patchError.c_str());
  }
}

// Captures a room's session off the request path (see CaptureNow).
void NRoomRouter::Capture(const std::string & name, const std::string & value, const std::string & revision)
{
  if (RoomBaseUrl(revision).empty()) return;
  auto done = std::make_shared<std::atomic<bool>>(false);
  SpawnWorker(name,
              std::thread([this, name, value, revision, done]() {
                CaptureNow(name, value, revision);
                done->store(true);
              }),
              done);
}

// Replays a room's stored session into it once it is ready - this is what brings an idle
// room back holding the file, navigator and drill-down it had. A room that is already in
// use is left alone: the live session always wins.
void NRoomRouter::ReplaySession(const std::string & value, json & payload)
{
  // Always hand back an object: several paths below return without touching the payload (nothing
  // open, nothing stored, the room unreachable), and callers read members out of it - on a null
  // payload that is a thrown type_error, which is not something a request should be able to do.
  payload = json::object();

  const std::string name = NRoomRouter::RoomName(fConfig, value);

  std::string text;
  std::string revision;
  std::string token;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it == fRooms.end()) return;
    text     = it->second.snapshot;
    revision = it->second.revision;
    // The router talks to a room with its read-write token: a room that was given tokens
    // refuses everything else, and this replay is just another client to it.
    token = it->second.tokenRw;
  }

  if (text.empty()) return;

  const std::string baseUrl = RoomBaseUrl(revision);
  if (baseUrl.empty()) return;

  json snapshot;
  if (!Ndmspc::NRoomSession::Decode(text, snapshot)) {
    NLogWarning("[room] ignoring an unreadable session snapshot for %s", name.c_str());
    return;
  }

  Ndmspc::NHttpRequest http;
  std::string         error;

  std::string                       file;
  const Ndmspc::NRoomSession::State state = Ndmspc::NRoomSession::Probe(http, baseUrl, file, error, token);
  if (state == Ndmspc::NRoomSession::State::Active) {
    NLogInfo("[room] %s already has '%s' open; keeping the live session", name.c_str(), file.c_str());
    payload["session"] = "live";
    return;
  }
  if (state == Ndmspc::NRoomSession::State::Unreachable) {
    NLogWarning("[room] cannot check %s before restoring its session: %s", name.c_str(), error.c_str());
    payload["restoreError"] = error;
    return;
  }

  if (!Ndmspc::NRoomSession::Restore(http, baseUrl, snapshot, error, token)) {
    NLogError("[room] cannot restore the session of %s: %s", name.c_str(), error.c_str());
    payload["restoreError"] = error;
    return;
  }

  NLogInfo("[room] restored the session of %s", name.c_str());
  payload["restored"] = true;
  payload["session"]  = "restored";
}

// ===========================================================================
//  Creating a room
// ===========================================================================
//
// Creating a room is slow - a Knative Service, its first revision, the HTTPRoute and the session
// replay - and ROOT's THttpServer serves one request at a time, so doing that work on the request
// thread froze the whole router for its duration: a room/list issued during a create came back
// only when the create had finished. The work now runs either on the request thread when the
// caller asked to wait (the historical behaviour, kept for scripts) or on a detached thread, with
// the room's progress published on its registry entry - which is what lets room/list and
// room/status report it, and several rooms be prepared at once.

// Reads the room's registry entry to decide whether its worker may carry on.
NRoomRouter::Abort NRoomRouter::CheckRunning(const std::string & name, int generation) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it == fRooms.end()) return Abort::Gone;
  if (it->second.cancel) return Abort::Cancelled;
  if (it->second.generation != generation) return Abort::Superseded;
  return Abort::None;
}

// Publishes the step a creation is in, for room/list and room/status.
void NRoomRouter::SetPhase(const std::string & name, const char * phase)
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it != fRooms.end()) it->second.phase = phase;
}

// Records how a creation ended and takes the room out of the preparing state.
void NRoomRouter::EnsureFinish(const std::string & name, const std::string & revision,
                                   const std::string & session, const std::string & error,
                                   const std::string & code)
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it == fRooms.end()) return;

  it->second.preparing  = false;
  it->second.finishedAt = NdmspcRoomNow();
  if (error.empty()) {
    it->second.phase = "ready";
    it->second.error.clear();
    it->second.code.clear();
    it->second.session = session;
    if (!revision.empty()) it->second.revision = revision;
  }
  else {
    it->second.phase = "failed";
    it->second.error = error;
    it->second.code  = code;
  }
}

// Handles a creation that has to stop, leaving nothing behind that it should not.
//
// A room that was closed while it was being created is deleted again (it may be half-created);
// one that a newer request superseded is left entirely to that request's worker.
bool NRoomRouter::Stopped(const std::string & name, int generation)
{
  if (CheckRunning(name, generation) == Abort::Cancelled) {
    NLogInfo("[room] %s was closed while it was being created", name.c_str());
    Delete(name);
  }

  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it != fRooms.end() && it->second.generation == generation) {
    it->second.preparing  = false;
    it->second.phase      = "cancelled";
    it->second.finishedAt = NdmspcRoomNow();
  }
  return false;
}

// Creates (or rolls) one room, publishing its progress as it goes.
//
// Holds no lock across the Kubernetes waits, and stops as soon as the room is closed or a newer
// request for it supersedes this one.
bool NRoomRouter::EnsureWorker(const std::string & value, int generation, Replay replay, json & payload,
                                   std::string & error, std::string & code)
{
  const NRoomConfig & cfg  = fConfig;
  const std::string        name = NRoomRouter::RoomName(cfg, value);

  // Fails the creation, recording why so that a client can read it back later. `why` carries the
  // stable code when the failure has one (name_conflict), and stays empty otherwise.
  const auto fail = [&](const std::string & message, const std::string & why = std::string()) {
    error = message;
    code  = why;
    EnsureFinish(name, "", "", message, why);
    return false;
  };

  if (cfg.apiServer.empty()) return fail("not running inside a cluster (KUBERNETES_SERVICE_HOST is unset)");

  if (CheckRunning(name, generation) != Abort::None) return Stopped(name, generation);

  SetPhase(name, "service");
  json skeleton;
  if (!Skeleton(skeleton, error)) return fail(error);

  const json access  = RoomAccess(name);
  const json service = NRoomRouter::ServiceObject(cfg, name, value, RouterBaseUrl(), access, RoomOwner(name), skeleton);
  std::string applyCode;
  if (!Apply(SvcCollection(), SvcPath(name), service, error, applyCode)) return fail(error, applyCode);

  // Closed while the Service was being created: the room is gone for good, so take the Service
  // back out rather than leaving one nothing else will ever clean up.
  if (CheckRunning(name, generation) == Abort::Gone) {
    Request("DELETE", SvcPath(name));
    return Stopped(name, generation);
  }

  SetPhase(name, "ready");
  std::string revision;
  // The wait fills in the code as well: it is the one step that can tell "no capacity" from a
  // plain timeout, and the reason has to reach the client either way.
  if (!WaitReady(name, revision, error, code)) {
    EnsureFinish(name, "", "", error, code);
    return false;
  }

  if (CheckRunning(name, generation) != Abort::None) return Stopped(name, generation);

  SetPhase(name, "route");
  const json route = NRoomRouter::RouteObject(cfg, name, value, revision, skeleton);
  if (!Apply(RouteCollection(), RoutePath(name), route, error, applyCode)) return fail(error, applyCode);

  // Publish the room before replaying its session: the replay talks to the room, so by then the
  // registry entry has to describe a room that exists.
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it == fRooms.end() || it->second.cancel || it->second.generation != generation) {
      return Stopped(name, generation);
    }
    it->second.name     = name;
    it->second.value    = value;
    it->second.revision = revision;
    it->second.lastSeen = NdmspcRoomNow();
  }

  // An object either way: with no replay nothing fills it, and the members are read below.
  json session = json::object();
  if (replay == Replay::Stored) {
    SetPhase(name, "restore");
    ReplaySession(value, session);
  }

  EnsureFinish(name, revision, session.value("session", std::string()), "", std::string());

  payload["room"]     = value;
  payload["name"]     = name;
  payload["revision"] = revision;
  payload["param"]    = cfg.param;
  payload["url"]      = NRoomRouter::ClientUrl(cfg, value, access.value("rw", ""));
  payload["access"]   = access;
  if (const std::string owner = RoomOwner(name); !owner.empty()) payload["owner"] = owner;
  payload["ttl"]      = cfg.idleTtlSec;
  payload["state"]    = "ready";
  if (session.contains("session")) payload["session"] = session["session"];
  return true;
}

// Starts creating (or rolling) a room, and reports what the client can act on.
//
// value  Room id chosen by the client.
// wait   kTRUE keeps the blocking answer - the call returns once the room is ready; kFALSE registers
//        the room as preparing and returns at once, leaving the work to a detached thread so the
//        router keeps serving every other request meanwhile.
// replay Whether the room's stored session is replayed once it is up.
// payload Receives the room's state and, once it is ready, its URL.
// error  Actionable reason when the call itself could not be started.
bool NRoomRouter::EnsureStart(const std::string & value, bool wait, Replay replay, json & payload,
                                  std::string & error, std::string & code)
{
  const NRoomConfig & cfg  = fConfig;
  const std::string        name = NRoomRouter::RoomName(cfg, value);
  if (cfg.apiServer.empty()) {
    error = "not running inside a cluster (KUBERNETES_SERVICE_HOST is unset)";
    return false;
  }

  int         generation = 0;
  long        startedAt  = 0;
  std::string tokenRw;
  std::string tokenRo;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    NRoomState &           state = fRooms[name];
    state.name                        = name;
    state.value                       = value;
    state.lastSeen                    = NdmspcRoomNow();

    // Minted once, when the room is first asked for, and kept for its whole life (and read back
    // from the Service after a router restart): a link that was handed out has to keep working.
    if (state.tokenRw.empty()) state.tokenRw = NRoomRouter::NewRoomToken();
    if (state.tokenRo.empty()) state.tokenRo = NRoomRouter::NewRoomToken();
    tokenRw = state.tokenRw;
    tokenRo = state.tokenRo;

    if (state.preparing) {
      // Already being prepared: report where it stands rather than starting a second worker.
      payload["room"]      = value;
      payload["name"]      = name;
      payload["revision"]  = state.revision;
      payload["param"]     = cfg.param;
      payload["url"]       = NRoomRouter::ClientUrl(cfg, value, state.tokenRw);
      payload["access"]    = NRoomRouter::AccessJson(state.tokenRw, state.tokenRo);
      if (!state.owner.empty()) payload["owner"] = state.owner;
      payload["ttl"]       = cfg.idleTtlSec;
      payload["state"]     = "preparing";
      payload["phase"]     = state.phase;
      payload["startedAt"] = state.startedAt;
      if (!state.error.empty()) payload["error"] = state.error;
      if (!state.code.empty()) payload["code"] = state.code;
      return true;
    }

    if (cfg.maxPreparing > 0) {
      int preparing = 0;
      for (const auto & entry : fRooms) {
        if (entry.second.preparing) ++preparing;
      }
      if (preparing >= cfg.maxPreparing) {
        // Bounded on purpose: every creation polls the cluster API, and the point of running them
        // off the request path is to keep the router responsive, not to accept unbounded work.
        error = std::to_string(preparing) + " rooms are already being prepared - try again when one is ready";
        return false;
      }
    }

    state.preparing  = true;
    state.phase      = "service";
    state.error.clear();
    state.code.clear();
    state.cancel     = false;
    state.startedAt  = NdmspcRoomNow();
    state.finishedAt = 0;
    state.generation += 1;
    generation = state.generation;
    startedAt  = state.startedAt;
  }

  if (wait) return EnsureWorker(value, generation, replay, payload, error, code);

  payload["room"]      = value;
  payload["name"]      = name;
  payload["param"]     = cfg.param;
  payload["url"]       = NRoomRouter::ClientUrl(cfg, value, tokenRw);
  payload["access"]    = NRoomRouter::AccessJson(tokenRw, tokenRo);
  if (const std::string owner = RoomOwner(name); !owner.empty()) payload["owner"] = owner;
  payload["ttl"]       = cfg.idleTtlSec;
  payload["state"]     = "preparing";
  payload["phase"]     = "service";
  payload["startedAt"] = startedAt;
  NLogInfo("[room] creating '%s' in the background", value.c_str());

  // The worker owns the room from here: it publishes the phase and the outcome on the registry
  // entry, which is what room/list and room/status report to every other client.
  ReapWorkers();
  auto done = std::make_shared<std::atomic<bool>>(false);
  SpawnWorker(value, std::thread([this, value, generation, replay, done]() {
                json        result;
                std::string failure;
                std::string code;
                try {
                  if (!EnsureWorker(value, generation, replay, result, failure, code) && !failure.empty()) {
                    NLogError("[room] '%s' could not be created: %s", value.c_str(), failure.c_str());
                  }
                }
                catch (const std::exception & e) {
                  // An exception escaping this thread would terminate the router, and there is no
                  // caller left to hand it to: report it on the room instead.
                  NLogError("[room] '%s' failed while it was being created: %s", value.c_str(), e.what());
                  EnsureFinish(NRoomRouter::RoomName(fConfig, value), "", "", e.what(), std::string());
                }
                done->store(true);
              }),
              done);
  return true;
}

// Whether this request wants to wait for the room: body "wait", query "wait", or the NDMSPC_ROOM_WAIT
// default.
bool NRoomRouter::WaitFlag(const NRoomConfig & cfg, const json & in)
{
  if (in.is_object()) {
    if (in.contains("wait")) {
      if (in["wait"].is_boolean()) return in["wait"].get<bool>();
      if (in["wait"].is_number_integer()) return in["wait"].get<int>() != 0;
      if (in["wait"].is_string()) return NRoomConfig::ParseBool(in["wait"].get<std::string>(), cfg.waitDefault);
    }
    if (in.contains("_query") && in["_query"].is_string()) {
      const auto params = NRoomRouter::ParseQuery(in["_query"].get<std::string>());
      const auto it     = params.find("wait");
      if (it != params.end()) return NRoomConfig::ParseBool(it->second, cfg.waitDefault);
    }
  }
  return cfg.waitDefault;
}

// Whether the router knows this room: it is in the registry - ensured, being prepared, or adopted.
//
// Deliberately cheap and registry-only: this is asked on every websocket upgrade, and the answer
// only has to be good enough to tell a client that its room is not there.
bool NRoomRouter::Tracked(const std::string & value) const
{
  const std::string name = NRoomRouter::RoomName(fConfig, value);
  std::lock_guard<std::mutex> lock(fMutex);
  return fRooms.find(name) != fRooms.end();
}

// The websocket policy of the router (gNdmspcWsConnectFilter).
//
// With rooms enabled this server is the router, and it serves no session of its own: a websocket
// that arrives here either forgot its ?room=<id> or names a room that does not exist - a connection
// for a room the router knows is routed to that room's pod by the gateway. Answering it would hand
// the client an empty session, so it is refused; the client sees a failed handshake (HTTP has no
// room for a body there) and the reason goes to the log.
std::string NRoomRouter::RoomParameter(const std::string & query, const std::string & param)
{
  const auto params = NRoomRouter::ParseQuery(query);
  const auto it     = params.find(param);
  return (it == params.end()) ? std::string() : it->second;
}

// The error a pod the scheduler cannot place produces, in the scheduler's own words.
std::string NRoomRouter::UnschedulableError(const std::string & reason)
{
  return "the cluster cannot schedule the room's pod: " + reason +
         " - free capacity, or lower the room's requests in its skeleton";
}

bool NRoomRouter::WsConnect(const std::string & query) const
{
  const std::string room = NRoomRouter::RoomParameter(query, fConfig.param);

  if (room.empty()) {
    NLogWarning("[room] refusing a websocket without ?%s=<id>: rooms are served by their own pod - "
                "open the room first (room/open)", fConfig.param.c_str());
    return kFALSE;
  }
  if (!Tracked(room)) {
    NLogWarning("[room] refusing a websocket for room '%s': the router is not tracking it - open it "
                "first (room/open)", room.c_str());
    return kFALSE;
  }
  return kTRUE;
}

std::string NRoomRouter::RequestId(json & in) const
{
  if (in.contains("room") && in["room"].is_string()) return in["room"].get<std::string>();
  if (in.contains("_query") && in["_query"].is_string()) {
    const auto params = NRoomRouter::ParseQuery(in["_query"].get<std::string>());
    const auto it     = params.find(fConfig.param);
    if (it != params.end()) return it->second;
    const auto legacy = params.find("room");
    if (legacy != params.end()) return legacy->second;
  }
  return {};
}

void NRoomRouter::Touch(const std::string & name)
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it == fRooms.end()) return;
  it->second.lastSeen = NdmspcRoomNow();
}

// ===========================================================================
//  Handlers
// ===========================================================================

// ===========================================================================
//  The cluster
// ===========================================================================

/**
 * @brief Reads a file, trimming the trailing newline (the ServiceAccount token and CA bundle).
 * @param path File to read.
 * @return The file's contents, or "" when it cannot be opened.
 */
static std::string NRoomReadFile(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string data = ss.str();
  while (!data.empty() && (data.back() == '\n' || data.back() == '\r')) data.pop_back();
  return data;
}

NHttpResponse NRoomClusterClient::Request(const std::string & method, const std::string & path,
                                          const std::string & body, const std::string & contentType)
{
  std::map<std::string, std::string> headers;
  headers["Authorization"] = "Bearer " + NRoomReadFile(fConfig.tokenFile);
  headers["Accept"]        = "application/json";
  headers["Content-Type"]  = contentType;

  // Never let a transport failure escape as an exception: the HTTP handler has no outer catch, so
  // a throw would abort the response and return nothing. status -1 marks "no HTTP response", with
  // the reason in body.
  try {
    Ndmspc::NHttpRequest http;
    return http.request(method, fConfig.apiServer + path, body, headers, "", "", "", fConfig.caFile, "", false);
  }
  catch (const std::exception & e) {
    Ndmspc::NHttpResponse failure;
    failure.status = -1;
    failure.body   = std::string("transport error: ") + e.what();
    NLogError("[room] %s %s failed: %s", method.c_str(), path.c_str(), e.what());
    return failure;
  }
}

// ===========================================================================
//  The router
// ===========================================================================

NRoomRouter::NRoomRouter()
    : fConfig(NRoomConfig::FromEnv()), fCluster(std::make_shared<NRoomClusterClient>(fConfig))
{
}

NRoomRouter::NRoomRouter(NRoomConfig config, std::shared_ptr<IRoomCluster> cluster)
    : fConfig(std::move(config)), fCluster(std::move(cluster))
{
}

NRoomRouter::~NRoomRouter()
{
  StopWorkers();
}

NRoomRouter & NRoomRouter::Instance()
{
  static NRoomRouter router;
  return router;
}

void NRoomRouter::SpawnWorker(const std::string & room, std::thread thread, std::shared_ptr<std::atomic<bool>> done)
{
  std::lock_guard<std::mutex> lock(fWorkerMutex);
  fWorkers.push_back(Worker{room, std::move(thread), std::move(done)});
}

void NRoomRouter::ReapWorkers(bool all)
{
  std::lock_guard<std::mutex> lock(fWorkerMutex);
  for (auto it = fWorkers.begin(); it != fWorkers.end();) {
    if (!all && (it->done == nullptr || !it->done->load())) {
      ++it;
      continue;
    }
    if (it->thread.joinable()) it->thread.join();
    it = fWorkers.erase(it);
  }
}

void NRoomRouter::StopWorkers()
{
  {
    std::lock_guard<std::mutex> lock(fMutex);
    for (auto & entry : fRooms) entry.second.cancel = true;
  }
  ReapWorkers(true);
}

int NRoomRouter::Workers() const
{
  std::lock_guard<std::mutex> lock(fWorkerMutex);
  int                         running = 0;
  for (const auto & worker : fWorkers) {
    // A finished worker is still tracked until it is reaped; only the running ones matter here.
    if (worker.done == nullptr || !worker.done->load()) ++running;
  }
  return running;
}

/**
 * @brief The websocket policy handed to the server: the process's router decides.
 * @param query The upgrade request's query string.
 * @return kTRUE when the router accepts the upgrade.
 */
static Bool_t NdmspcRoomWsFilter(const std::string & query)
{
  return NRoomRouter::Instance().WsConnect(query) ? kTRUE : kFALSE;
}

std::map<std::string, NRoomState> NRoomRouter::Rooms() const
{
  std::lock_guard<std::mutex> lock(fMutex);
  return fRooms;
}

bool NRoomRouter::Preparing(const std::string & value) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(RoomName(fConfig, value));
  return it != fRooms.end() && it->second.preparing;
}

int NRoomRouter::PreparingCount() const
{
  std::lock_guard<std::mutex> lock(fMutex);
  int                         count = 0;
  for (const auto & entry : fRooms) {
    if (entry.second.preparing) ++count;
  }
  return count;
}

bool NRoomRouter::KubernetesAvailable(std::string * reason)
{
  // Rooms are a Kubernetes feature: the router creates Knative Services and HTTPRoutes through the
  // in-cluster API, and the API server comes from KUBERNETES_SERVICE_HOST/PORT - the same environment
  // that tells the router it is running in a cluster at all (see NRoomConfig::FromEnv).
  if (!NRoomConfig::FromEnv().apiServer.empty()) return true;
  if (reason != nullptr) {
    *reason = "rooms are supported only inside Kubernetes "
              "(KUBERNETES_SERVICE_HOST is not set). Start without rooms "
              "(--rooms false / NDMSPC_ROOMS unset) or run in a cluster.";
  }
  return false;
}

bool NRoomRouter::Register(NHttpServer * server)
{
  // Say why and refuse instead of serving /api/room/* actions that would all fail.
  std::string reason;
  if (!KubernetesAvailable(&reason)) {
    NLogError("[room] %s", reason.c_str());
    return false;
  }

  // The policy is process-wide for now; the handler asks this router (see NRoomRouter.h).
  (void)server;

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // This server is the router, and it is not a client endpoint: a websocket must name a room the
  // router knows (see NdmspcRoomWsConnectFilter). A server without rooms leaves this unset and
  // keeps accepting every websocket.
  Ndmspc::gNdmspcWsConnectFilter = NdmspcRoomWsFilter;

  Ndmspc::RegisterMcpTool("room/open", {
      .description = "Ensure a room exists (one Knative Service per room) and return the URL that "
                     "serves it. POST/GET with 'room' in the body. With wait=false the call returns "
                     "at once with state=preparing and the room is created in the background - poll "
                     "room/status or room/list for the outcome.",
      .methods     = {"GET", "POST"},
      .inputSchema = {{"properties",
                       {{"room", {{"type", "string"}, {"description", "Room id (any client-chosen string)."}}},
                        {"wait",
                         {{"type", "boolean"},
                          {"description", "Wait for the room to be ready before answering (default true); "
                                          "false starts the creation in the background."}}}},
                       }},
  });
  Ndmspc::RegisterMcpTool("room/status", {
      .description = "Report whether a room is known to the router, its current revision, and - while "
                     "it is being created - where that creation is (state=preparing with "
                     "phase=service|ready|route|restore), or state=failed with the reason.",
      .methods     = {"GET"},
      .inputSchema = {{"properties", {{"room", {{"type", "string"}}}}}},
  });
  Ndmspc::RegisterMcpTool("room/list", {
      .description = "List the rooms the router is currently tracking (with their last-seen time). A "
                     "room whose creation is still running is listed as well, with state=preparing and "
                     "the phase it has reached; one whose creation failed is listed with state=failed "
                     "and the error.",
      .methods     = {"GET"},
  });
  Ndmspc::RegisterMcpTool("room/close", {
      .description = "Delete a room's HTTPRoute and Knative Service immediately.",
      .methods     = {"DELETE"},
      .inputSchema = {{"properties", {{"room", {{"type", "string"}}}}}},
  });
  // Internal plumbing: a room reports its session here and fetches it back when it wakes.
  // Hidden because it is not a user-facing room action.
  Ndmspc::RegisterMcpTool("room/state", {
      .description = "Internal: store or fetch a room's session snapshot.",
      .methods     = {"GET", "POST"},
      .hidden      = true,
      .inputSchema = {{"properties", {{"room", {{"type", "string"}}}}}},
  });
  Ndmspc::RegisterMcpTool("room/backup", {
      .description = "Export every tracked room and its session as one JSON document, for backup or "
                     "for restoring onto another deployment. It carries no ROOT data, only the "
                     "session (file, navigator, drill-down).",
      .methods     = {"GET"},
  });
  Ndmspc::RegisterMcpTool("room/restore", {
      .description = "Ensure every room named in a document from room/backup and replay its session. "
                     "Additive: rooms already present are left alone, nothing is deleted, and a "
                     "live session is never overwritten.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"document",
                         {{"type", "object"},
                          {"description", "A document produced by room/backup."}}}}}},
  });

  // -------------------------------------------------------------------------
  //  /api/room/open — ensure a room and hand back its URL
  // -------------------------------------------------------------------------
  handlers["room/open"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleOpen(method, in, out);
  };
  // -------------------------------------------------------------------------
  //  /api/room/status — report a room's state
  // -------------------------------------------------------------------------
  handlers["room/status"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleStatus(method, in, out);
  };
  // -------------------------------------------------------------------------
  //  /api/room/list — rooms the router is tracking
  // -------------------------------------------------------------------------
  handlers["room/list"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleList(method, in, out);
  };
  // -------------------------------------------------------------------------
  //  /api/room/close — delete a room
  // -------------------------------------------------------------------------
  handlers["room/close"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleClose(method, in, out);
  };
  // -------------------------------------------------------------------------
  //  /api/room/state — the session snapshot a room reports and fetches back
  // -------------------------------------------------------------------------
  //
  // Not a user-facing action: a room pushes its session here after it changes, and reads it
  // back when it starts, so a room that scaled to zero can come back as it was left.
  //
  // The room id travels in the body, never as a query: a `?room=` query would be matched by
  // the room's own HTTPRoute and routed to the room instead of to the router. A POST that
  // carries a `snapshot` stores it; a POST without one reads the stored snapshot back. The
  // read is a POST because NDMSPC's HTTP client forwards the body for POST but not for GET,
  // so a GET could only ever be used by hand with curl (GET is still accepted for that).
  handlers["room/state"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleState(method, in, out);
  };
  // -------------------------------------------------------------------------
  //  /api/room/backup — export the rooms and their sessions as one document
  // -------------------------------------------------------------------------
  //
  // The document holds the room set and each room's session, so it can be restored onto this
  // deployment after losing the rooms, or onto a new one. It is not a Kubernetes manifest dump:
  // rooms are re-created from the current skeleton and their routes re-pinned, because a
  // backed-up HTTPRoute pins a revision name that will not exist after a rebuild. It carries no
  // data - ROOT files are not persisted - so what comes back is the session, not files.
  //
  // Nothing is called on the rooms, so exporting never wakes an idle room.
  handlers["room/backup"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleBackup(method, in, out);
  };
  // -------------------------------------------------------------------------
  //  /api/room/restore — ensure every room in a document and replay its session
  // -------------------------------------------------------------------------
  //
  // Additive and convergent: rooms are created (or rolled) from the current skeleton and their
  // sessions replayed, rooms not named in the document are untouched, nothing is deleted, and a
  // room that already has a file open is left alone. Per-room failures are reported rather than
  // aborting the whole restore.
  handlers["room/restore"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                             std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleRestore(method, in, out);
  };
  return true;
}

// ===========================================================================
//  room/open
// ===========================================================================
void NRoomRouter::HandleOpen(const std::string & method, json & in, json & out)
{
  Adopt();
  Sweep();

  if (!(method.find("GET") != std::string::npos || method.find("POST") != std::string::npos)) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/open";
    return;
  }

  const std::string id = RequestId(in);
  if (id.empty()) {
    out["result"] = "failure";
    out["error"]  = "Missing room id (send it in the body as {\"room\": \"<id>\"})";
    return;
  }

  // A room belongs to whoever creates it, and room/open is also how a client obtains a room's link,
  // so this is where both halves of that rule are applied: a room the router does not know yet is
  // claimed by the caller, and a room that already belongs to someone else is refused to an
  // identified caller who is not an admin. A room that predates ownership stays unowned: nobody
  // identified themselves when it was created, so there is nobody to give it to.
  const NRequestIdentity identity = RequestIdentity(in);
  const std::string     name      = NRoomRouter::RoomName(fConfig, id);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it == fRooms.end()) {
      if (!identity.Empty()) {
        NRoomState & state = fRooms[name];
        state.name         = name;
        state.value        = id;
        state.owner        = identity.Owner();
      }
    }
    else if (!MaySee(it->second, identity)) {
      out["result"] = "failure";
      out["code"]   = kNotOwner;
      out["error"]  = "Room '" + id + "' belongs to " + it->second.owner;
      return;
    }
  }

  // Creating a room is slow and the router serves one request at a time, so the caller decides
  // whether to wait for it (the default, which is what scripts relied on) or to be told that it
  // is being prepared and poll room/status: the work then runs off the request path, which keeps
  // the router answering everyone else and lets several rooms be prepared at once. The session
  // replay - the call that wakes the room - happens inside that work either way.
  const bool wait = NRoomRouter::WaitFlag(fConfig, in);
  json        payload;
  std::string error;
  std::string code;
  bool        started = false;
  try {
    started = EnsureStart(id, wait, Replay::Stored, payload, error, code);
  }
  catch (const std::exception & e) {
    // A failure here must not take the router down with it: report it like any other.
    error   = std::string("cannot create the room: ") + e.what();
    code.clear();
    started = false;
  }
  if (!started) {
    NLogError("[room] open failed for '%s': %s", id.c_str(), error.c_str());
    out["result"] = "failure";
    out["error"]  = error;
    if (!code.empty()) out["code"] = code;
    return;
  }
  if (payload.value("state", std::string()) == "preparing") {
    NLogInfo("[room] room '%s' is being prepared (%s)", id.c_str(), payload.value("phase", "").c_str());
  }
  else {
    NLogInfo("[room] room '%s' ready (%s)", id.c_str(), payload.value("revision", "").c_str());
  }

  out["result"]  = "success";
  out["payload"] = payload;
}

// ===========================================================================
//  room/status
// ===========================================================================
void NRoomRouter::HandleStatus(const std::string & method, json & in, json & out)
{
  if (method.find("GET") == std::string::npos) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/status";
    return;
  }

  const std::string id = RequestId(in);
  if (id.empty()) {
    out["result"] = "failure";
    out["error"]  = "Missing room id";
    return;
  }

  const std::string name = NRoomRouter::RoomName(fConfig, id);
  Touch(name);

  // Somebody else's room is not this caller's to look at: its answer carries the room's links and
  // the session it is in.
  const NRequestIdentity identity = RequestIdentity(in);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it != fRooms.end() && !MaySee(it->second, identity)) {
      out["result"] = "failure";
      out["code"]   = kNotOwner;
      out["error"]  = "Room '" + id + "' belongs to " + it->second.owner;
      return;
    }
  }

  out["result"]       = "success";
  out["payload"]["room"]     = id;
  out["payload"]["name"]     = name;
  out["payload"]["param"]    = fConfig.param;

  // What its creation is doing, if anything: a room is registered as soon as its creation
  // starts, so a client can say what it is waiting for rather than only that it is not ready.
  std::string state;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    out["payload"]["tracked"]      = (it != fRooms.end());
    if (it != fRooms.end()) {
      out["payload"]["revision"]    = it->second.revision;
      out["payload"]["lastSeen"]    = it->second.lastSeen;
      out["payload"]["hasSnapshot"] = !it->second.snapshot.empty();
      // The links that open this room: a client needs them to hand one out, and until the room is
      // adopted they are the only record of the tokens.
      const json access = NRoomRouter::AccessJson(it->second.tokenRw, it->second.tokenRo);
      if (!access.empty()) out["payload"]["access"] = access;
      if (!it->second.owner.empty()) out["payload"]["owner"] = it->second.owner;
      // How the last creation left the room's session: a client that waited for a create with
      // wait=false follows it through here, and what the replay did belongs in that answer.
      if (!it->second.session.empty()) out["payload"]["session"] = it->second.session;
      if (it->second.preparing) {
        out["payload"]["preparing"] = true;
        out["payload"]["phase"]     = it->second.phase;
        out["payload"]["startedAt"] = it->second.startedAt;
        state                       = "preparing";
      }
      else if (it->second.phase == "failed" && !it->second.error.empty()) {
        out["payload"]["phase"] = it->second.phase;
        out["payload"]["error"] = it->second.error;
        if (!it->second.code.empty()) out["payload"]["code"] = it->second.code;
        state = "failed";
      }
    }
  }

  // Report the live cluster view too - for a room that is still being prepared, it is simply
  // not there yet.
  const auto response = Request("GET", SvcPath(name));
  out["payload"]["exists"] = (response.status == 200);
  if (response.status == 200) {
    try {
      const json svc    = json::parse(response.body);
      const json status = svc.value("status", json::object());
      bool       ready  = false;
      for (const auto & condition : status.value("conditions", json::array())) {
        if (condition.value("type", "") == "Ready") ready = (condition.value("status", "") == "True");
      }
      out["payload"]["ready"]    = ready;
      out["payload"]["revision"] = status.value("latestReadyRevisionName", "");
      if (state.empty()) state   = ready ? "ready" : "not ready";
    }
    catch (const std::exception &) {
    }
  }
  if (!state.empty()) out["payload"]["state"] = state;
}

// ===========================================================================
//  room/list
// ===========================================================================
void NRoomRouter::HandleList(const std::string & method, json & in, json & out)
{
  if (method.find("GET") == std::string::npos) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/list";
    return;
  }

  // Who is asking decides what the list holds: see "Ownership and visibility".
  const NRequestIdentity identity = RequestIdentity(in);

  json                     rooms = json::array();
  std::vector<std::string> stale;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    for (const auto & entry : fRooms) {
      // Filtered here, before the live status below: a room the caller may not see is also a room
      // whose pod must not be probed (which would wake it) or whose session must not be captured.
      if (!MaySee(entry.second, identity)) continue;
      json room;
      room["name"]     = entry.second.name;
      room["room"]     = entry.second.value;
      room["revision"] = entry.second.revision;
      room["lastSeen"] = entry.second.lastSeen;
      const json access = NRoomRouter::AccessJson(entry.second.tokenRw, entry.second.tokenRo);
      if (!access.empty()) room["access"] = access;
      if (!entry.second.owner.empty()) room["owner"] = entry.second.owner;
      // A room is listed from the moment its creation starts, carrying what it is waiting for,
      // so a client can show the wait instead of an absent room.
      if (entry.second.preparing) {
        room["preparing"] = true;
        room["phase"]     = entry.second.phase;
        room["startedAt"] = entry.second.startedAt;
        room["state"]     = "preparing";
      }
      else if (entry.second.phase == "failed" && !entry.second.error.empty()) {
        room["phase"] = entry.second.phase;
        room["error"] = entry.second.error;
        if (!entry.second.code.empty()) room["code"] = entry.second.code;
        room["state"] = "failed";
      }
      rooms.push_back(room);
    }
  }

  // Report live state, and drop rooms whose Service has gone away (deleted out
  // of band). A room keeps its Service when idle but scales to zero pods
  // (min-scale 0), so "replicas: 0 / active: false" is the normal resting state
  // - worth showing explicitly rather than leaving the caller guessing.
  for (auto & room : rooms) {
    const std::string name = room.value("name", "");

    // A room that is being prepared, or whose creation failed, has no live view yet: asking for
    // its Service would report a 404 for one that was never created, and a room is only expiring
    // through the idle TTL. Its state is what was reported above.
    const std::string state = room.value("state", std::string());
    if (state == "preparing" || state == "failed") continue;

    const auto response = Request("GET", SvcPath(name));
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
      room["state"]    = ready ? "ready" : "not ready";

      const std::string revision = room.value("revision", "");
      if (!revision.empty()) {
        const auto rev = Request("GET", RevisionPath(revision));
        if (rev.status == 200) {
          const int replicas = json::parse(rev.body)
                                   .value("status", json::object())
                                   .value("actualReplicas", 0);
          room["replicas"] = replicas;
          room["active"]   = replicas > 0;
          // Capture the session while the room is running. This loop already knows
          // whether the room has pods, which is what makes it safe to talk to it: a
          // request to a scaled-to-zero room would wake it.
          if (replicas > 0) {
            Capture(room.value("name", ""), room.value("room", ""), revision);
          }
        }
      }
    }
    catch (const std::exception &) {
    }
  }
  for (const auto & name : stale) {
    NLogInfo("[room] dropping %s: Service no longer exists", name.c_str());
    {
      std::lock_guard<std::mutex> lock(fMutex);
      fRooms.erase(name);
    }
    Request("DELETE", RoutePath(name));
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
  out["payload"]["ttl"]   = fConfig.idleTtlSec;
}

// ===========================================================================
//  room/close
// ===========================================================================
void NRoomRouter::HandleClose(const std::string & method, json & in, json & out)
{
  if (method.find("DELETE") == std::string::npos) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/close";
    return;
  }

  const std::string id = RequestId(in);
  if (id.empty()) {
    out["result"] = "failure";
    out["error"]  = "Missing room id";
    return;
  }

  // Deleting someone else's room is not this caller's to do.
  const NRequestIdentity identity = RequestIdentity(in);
  const std::string     name      = NRoomRouter::RoomName(fConfig, id);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it != fRooms.end() && !MaySee(it->second, identity)) {
      out["result"] = "failure";
      out["code"]   = kNotOwner;
      out["error"]  = "Room '" + id + "' belongs to " + it->second.owner;
      return;
    }
  }

  CloseRoom(id); // a room that is being created is cancelled on the way out
  out["result"]  = "success";
  out["payload"]["room"] = id;
  out["payload"]["name"] = name;
}

// ===========================================================================
//  room/state
// ===========================================================================
void NRoomRouter::HandleState(const std::string & method, json & in, json & out)
{
  const std::string id = RequestId(in);
  if (id.empty()) {
    out["result"] = "failure";
    out["error"]  = "Missing room id (send it in the body, not in the query)";
    return;
  }
  const std::string name = NRoomRouter::RoomName(fConfig, id);

  const bool isPost   = method.find("POST") != std::string::npos;
  const bool isGet    = method.find("GET") != std::string::npos;
  const json snapshot = (in.is_object() && in.contains("snapshot")) ? in["snapshot"] : json();

  if (isPost && snapshot.is_object() && !snapshot.empty()) {
    const std::string text = Ndmspc::NRoomSession::Encode(snapshot);
    if (text.empty()) {
      out["result"] = "failure";
      out["error"]  = "The session snapshot is empty or too large to store";
      return;
    }
    StoreSnapshot(name, id, text);
    NLogInfo("[room] %s reported its session (file '%s')", name.c_str(),
             snapshot.value("file", std::string()).c_str());
    out["result"]          = "success";
    out["payload"]["room"] = id;
    out["payload"]["name"] = name;
    return;
  }

  if (isPost || isGet) {
    const std::string text         = Snapshot(name, id);
    json              stored;
    const bool        hasSnapshot = Ndmspc::NRoomSession::Decode(text, stored);
    out["result"]                 = "success";
    out["payload"]["room"]        = id;
    out["payload"]["name"]        = name;
    out["payload"]["hasSnapshot"] = hasSnapshot;
    if (hasSnapshot) out["payload"]["snapshot"] = stored;
    return;
  }

  out["result"] = "failure";
  out["error"]  = "Unsupported HTTP method for room/state";
}

// ===========================================================================
//  room/backup
// ===========================================================================
void NRoomRouter::HandleBackup(const std::string & method, json & in, json & out)
{
  if (method.find("GET") == std::string::npos && method.find("POST") == std::string::npos) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/backup";
    return;
  }

  const NRoomConfig &   cfg      = fConfig;
  const NRequestIdentity identity = RequestIdentity(in);

  // Copy the registry under the lock, then read each session outside it. A caller exports the rooms
  // it may see: an admin exports all of them, and anyone else their own.
  std::vector<NRoomState> rooms;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    for (const auto & entry : fRooms) {
      if (!MaySee(entry.second, identity)) continue;
      rooms.push_back(entry.second);
    }
  }

  json document;
  document["version"]             = 1;
  document["createdAt"]           = NdmspcRoomNow();
  document["router"]["namespace"] = cfg.ns;
  document["router"]["prefix"]    = cfg.prefix;
  document["router"]["param"]     = cfg.param;
  document["router"]["ttl"]       = cfg.idleTtlSec;
  document["rooms"]               = json::array();

  for (const auto & room : rooms) {
    json entry;
    entry["room"]     = room.value;
    entry["name"]     = room.name;
    entry["revision"] = room.revision; // informational: recomputed when the room is restored
    entry["lastSeen"] = room.lastSeen; // informational: reset when the room is restored

    // Carry the tokens, so that restoring hands out the same links again instead of quietly
    // minting new ones - which would roll the restored room's revision as well.
    const json access = NRoomRouter::AccessJson(room.tokenRw, room.tokenRo);
    if (!access.empty()) entry["access"] = access;

    // And the owner, so the room comes back as the same person's rather than as nobody's.
    if (!room.owner.empty()) entry["owner"] = room.owner;

    json       snapshot;
    const bool hasSnapshot = Ndmspc::NRoomSession::Decode(Snapshot(room.name, room.value), snapshot);
    if (hasSnapshot) entry["snapshot"] = snapshot;
    document["rooms"].push_back(std::move(entry));
  }

  NLogInfo("[room] exported %zu room(s)", rooms.size());
  out["result"]  = "success";
  out["payload"] = document;
}

// ===========================================================================
//  room/restore
// ===========================================================================
void NRoomRouter::HandleRestore(const std::string & method, json & in, json & out)
{
  if (method.find("POST") == std::string::npos) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/restore";
    return;
  }

  // The document is the request body itself, or under "document" (how the MCP tool carries it).
  const json document = in.is_object() && in.contains("document") ? NdmspcRoomMember(in, "document") : in;
  if (!document.is_object()) {
    out["result"] = "failure";
    out["error"]  = "Missing the restore document";
    return;
  }
  const json rooms = NdmspcRoomMember(document, "rooms");
  if (!rooms.is_array()) {
    out["result"] = "failure";
    out["error"]  = "Missing 'rooms' array in the restore document";
    return;
  }

  const int version = NdmspcRoomInt(document, "version", 1);
  if (version != 1) {
    out["result"] = "failure";
    out["error"]  = "Unsupported restore document version " + std::to_string(version);
    return;
  }

  const NRoomConfig & cfg    = fConfig;
  const json               router = NdmspcRoomMember(document, "router");
  if (router.is_object()) {
    const std::string param  = router.value("param", cfg.param);
    const std::string prefix = router.value("prefix", cfg.prefix);
    if (param != cfg.param || prefix != cfg.prefix) {
      out["result"] = "failure";
      out["error"]  = "The document came from a router configured differently (param '" + param +
                      "', prefix '" + prefix + "'); it would create wrongly named rooms here";
      return;
    }
  }

  json restored = json::array();
  json failed   = json::array();

  const NRequestIdentity identity = RequestIdentity(in);

  for (const auto & entry : rooms) {
    if (!entry.is_object()) continue;
    const std::string id = entry.value("room", "");
    if (id.empty()) continue;

    // Take the document's owner and tokens first, so the room comes back as it was exported - the
    // same person's room, with the same links - rather than as a new one: the registry is what the
    // room's Service object is built from. A room that already exists and belongs to someone else is
    // not this caller's to restore.
    const json        owner     = NdmspcRoomMember(entry, "owner");
    const std::string ownerText = owner.is_string() ? owner.get<std::string>() : std::string();
    {
      const std::string           name = NRoomRouter::RoomName(cfg, id);
      std::lock_guard<std::mutex> lock(fMutex);
      const auto                  it = fRooms.find(name);
      if (it != fRooms.end() && !MaySee(it->second, identity)) {
        json refusal;
        refusal["room"]  = id;
        refusal["name"]  = name;
        refusal["code"]  = kNotOwner;
        refusal["error"] = "Room '" + id + "' belongs to " + it->second.owner;
        failed.push_back(std::move(refusal));
        continue;
      }
      NRoomState & state = fRooms[name];
      if (state.owner.empty() && !ownerText.empty()) state.owner = ownerText;
      const json access = NdmspcRoomMember(entry, "access");
      if (access.is_object() && !access.empty()) {
        if (state.tokenRw.empty()) state.tokenRw = access.value("rw", "");
        if (state.tokenRo.empty()) state.tokenRo = access.value("ro", "");
      }
    }

    json        payload;
    std::string error;
    // Blocking, and without a session replay: a restore has to be finished before it answers,
    // and the document's session is stored below and replayed by the call after that.
    bool        ensured = false;
    std::string code;
    try {
      ensured = EnsureStart(id, /*wait=*/true, Replay::None, payload, error, code);
    }
    catch (const std::exception & e) {
      error   = std::string("cannot create the room: ") + e.what();
      code.clear();
      ensured = false;
    }
    if (!ensured) {
      NLogError("[room] restore of '%s' failed: %s", id.c_str(), error.c_str());
      json failure;
      failure["room"]  = id;
      failure["error"] = error;
      if (!code.empty()) failure["code"] = code;
      failed.push_back(std::move(failure));
      continue;
    }

    // Keep the session with the room: in the registry, on the Service annotation (so it
    // survives this process), and replayed into the room itself.
    const json snapshot = NdmspcRoomMember(entry, "snapshot");
    if (snapshot.is_object() && !snapshot.empty()) {
      const std::string text = Ndmspc::NRoomSession::Encode(snapshot);
      if (!text.empty()) StoreSnapshot(NRoomRouter::RoomName(cfg, id), id, text);
    }

    json session;
    ReplaySession(id, session); // left alone when the room already has a file open

    json done;
    done["room"]     = id;
    done["name"]     = payload.value("name", "");
    done["revision"] = payload.value("revision", "");
    done["session"]  = session.value("session", "");
    if (session.contains("restoreError")) {
      done["error"] = session["restoreError"];
      failed.push_back(std::move(done));
    }
    else {
      restored.push_back(std::move(done));
    }
  }

  NLogInfo("[room] restored %zu room(s), %zu failed", restored.size(), failed.size());
  out["result"]              = "success";
  out["payload"]["restored"] = restored;
  out["payload"]["failed"]   = failed;
}

} // namespace Ndmspc

