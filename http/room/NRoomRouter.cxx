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

// What a room's Service declares its container may use: the cpu and memory of its requests and
// limits, spelled exactly as Kubernetes holds them ("500m", "2", "512Mi"). The skeleton the router
// stamped onto the Service is the only source, so a room that declares nothing answers an empty
// object and its payload carries no `resources` at all. Nothing here reports *use*: that needs the
// cluster's metrics API, and a scaled-to-zero room has no pod to measure anyway.
json NRoomRouter::ServiceResources(const json & service)
{
  json out = json::object();
  if (!service.is_object()) return out;

  const json spec       = service.value("spec", json::object());
  const json tmpl       = spec.is_object() ? spec.value("template", json::object()) : json::object();
  const json podSpec    = tmpl.is_object() ? tmpl.value("spec", json::object()) : json::object();
  const json containers = podSpec.is_object() ? podSpec.value("containers", json::array()) : json::array();
  if (!containers.is_array() || containers.empty() || !containers.at(0).is_object()) return out;

  const json resources = containers.at(0).value("resources", json::object());
  if (!resources.is_object()) return out;

  for (const char * key : {"requests", "limits"}) {
    const json declared = resources.value(key, json::object());
    if (!declared.is_object()) continue;

    json entry = json::object();
    for (const char * field : {"cpu", "memory"}) {
      if (declared.contains(field)) entry[field] = declared[field];
    }
    if (!entry.empty()) out[key] = entry;
  }

  return out;
}

// The profiles a skeleton defines, and the names they answer to, as one message for the error.
namespace {
std::string KnownProfiles(const json & profiles)
{
  std::string list;
  for (auto it = profiles.begin(); it != profiles.end(); ++it) {
    if (!list.empty()) list += ", ";
    list += it.key();
  }
  return list;
}
} // namespace

// Which profile a room is created with: the one this open asked for, or the skeleton's default.
//
// The names are the deployment's - the router never knows what "small" means - so this only decides
// *which* one, and `ProfileResources` says what it is worth. A skeleton that defines no profiles at
// all answers an empty name and no error: that is a deployment which offers no sizes, and rooms are
// then whatever their own spec declares (what every room was before profiles existed).
std::string NRoomRouter::ProfileName(const json & skeleton, const std::string & requested, std::string & error)
{
  const json profiles = skeleton.value("profiles", json::object());
  if (!profiles.is_object() || profiles.empty()) return std::string();

  std::string name = requested;
  if (name.empty()) name = skeleton.value("defaultProfile", std::string());
  if (name.empty()) {
    error = "the room skeleton defines profiles but no defaultProfile to fall back on";
    return std::string();
  }
  if (!profiles.contains(name) || !profiles[name].is_object()) {
    error = "unknown room profile '" + name + "' (this deployment offers: " + KnownProfiles(profiles) + ")";
    return std::string();
  }
  return name;
}

// What a profile allows a room to use, from the skeleton.
json NRoomRouter::ProfileResources(const json & skeleton, const std::string & name, std::string & error)
{
  if (name.empty()) return json::object();

  const json profiles = skeleton.value("profiles", json::object());
  if (!profiles.is_object() || !profiles.contains(name) || !profiles[name].is_object()) {
    error = "unknown room profile '" + name + "' (this deployment offers: " + KnownProfiles(profiles) + ")";
    return json::object();
  }

  const json resources = profiles[name].value("resources", json::object());
  if (!resources.is_object() || resources.empty()) {
    error = "room profile '" + name + "' declares no resources";
    return json::object();
  }
  return resources;
}

namespace {
// A Kubernetes quantity's number and its suffix ("250m", "1", "1.5Gi"), so both readers below start
// the same way: Kubernetes writes a decimal number followed by a unit, and nothing else. Junk leaves
// the suffix as it found it - which is why the callers can tell "no number" from "no unit".
double QuantityNumber(const std::string & quantity, std::string & suffix)
{
  suffix.clear();
  if (quantity.empty()) return 0;

  char *       end{nullptr};
  const double value = std::strtod(quantity.c_str(), &end);
  if (end == nullptr || end == quantity.c_str()) return 0; // no number at all
  suffix = end;
  return value;
}
} // namespace

// A Kubernetes CPU quantity in milli-cores (0 when it is not one).
long NRoomRouter::CpuMillis(const std::string & quantity)
{
  std::string  suffix;
  const double value = QuantityNumber(quantity, suffix);
  if (suffix.empty()) return static_cast<long>(value * 1000.0 + 0.5);   // plain cores: "1", "1.5"
  if (suffix == "m") return static_cast<long>(value + 0.5);             // milli-cores: "250m"
  if (suffix == "u") return static_cast<long>(value / 1000.0 + 0.5);    // micro-cores
  if (suffix == "n") return static_cast<long>(value / 1000000.0 + 0.5); // nano-cores
  return 0;
}

// A Kubernetes memory quantity in bytes (0 when it is not one).
long NRoomRouter::Bytes(const std::string & quantity)
{
  std::string  suffix;
  const double value = QuantityNumber(quantity, suffix);
  if (value <= 0) return 0;
  if (suffix.empty()) return static_cast<long>(value + 0.5); // plain bytes

  // The power-of-two suffixes and their decimal twins, exactly as Kubernetes defines them.
  const std::pair<const char *, double> suffixes[] = {{"Ki", 1024.0},
                                                      {"Mi", 1024.0 * 1024},
                                                      {"Gi", 1024.0 * 1024 * 1024},
                                                      {"Ti", 1024.0 * 1024 * 1024 * 1024},
                                                      {"Pi", 1024.0 * 1024 * 1024 * 1024 * 1024},
                                                      {"Ei", 1024.0 * 1024 * 1024 * 1024 * 1024 * 1024},
                                                      {"K", 1000.0},
                                                      {"k", 1000.0},
                                                      {"M", 1000.0 * 1000},
                                                      {"G", 1000.0 * 1000 * 1000},
                                                      {"T", 1000.0 * 1000 * 1000 * 1000},
                                                      {"P", 1000.0 * 1000 * 1000 * 1000 * 1000}};
  for (const auto & [name, factor] : suffixes) {
    if (suffix != name) continue;
    return static_cast<long>(value * factor + 0.5);
  }
  return 0;
}

// A Kubernetes timestamp (RFC 3339, seconds precision) as epoch seconds; 0 when it is not one.
//
// Kubernetes writes these itself and always in this shape ("2026-09-23T14:17:01Z"), so reading the
// fields positionally is enough - and portable, where strptime/timegm are libc extensions this
// project's strict -std=c++23 does not declare.
long NRoomRouter::Rfc3339(const std::string & value)
{
  int year{0}, month{0}, day{0}, hour{0}, minute{0}, second{0};
  if (std::sscanf(value.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute,
                  &second) != 6) {
    return 0;
  }

  const std::chrono::year_month_day date{std::chrono::year{year},
                                         std::chrono::month{static_cast<unsigned>(month)},
                                         std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok() || hour > 23 || minute > 59 || second > 60) return 0;

  const std::chrono::sys_days days{date};
  const std::chrono::sys_seconds seconds = std::chrono::time_point_cast<std::chrono::seconds>(days) +
                                           std::chrono::hours{hour} + std::chrono::minutes{minute} +
                                           std::chrono::seconds{second};
  return static_cast<long>(seconds.time_since_epoch().count());
}

// Whether a container termination reason is a failure: a container that finished its work is not one,
// and neither is a pod that never said why.
bool NRoomRouter::FailedReason(const std::string & reason)
{
  return !reason.empty() && reason != "Completed";
}

namespace {
// The termination a container reports, wherever it reports it: `state` while it is still down,
// `lastState` once it has come back - and only in `state` does a killed container stay down. The
// restart count travels with it, since it belongs to the same story.
json ContainerTermination(const json & container, const char * where)
{
  json terminated = container.value(where, json::object()).value("terminated", json::object());
  if (terminated.is_object()) {
    terminated["restartCount"] = container.value("restartCount", 0);
  }
  return terminated;
}
} // namespace

// Why a pod's container last died: the newest failure among its containers.
json NRoomRouter::PodTermination(const json & pod)
{
  const json statuses = pod.value("status", json::object()).value("containerStatuses", json::array());
  if (!statuses.is_array()) return json::object();

  json newest;
  long newestAt = -1;
  for (const auto & container : statuses) {
    if (!container.is_object()) continue;
    for (const char * where : {"lastState", "state"}) {
      const json terminated = ContainerTermination(container, where);
      const std::string reason = terminated.value("reason", std::string());
      if (!NRoomRouter::FailedReason(reason)) continue;

      const long at = NRoomRouter::Rfc3339(terminated.value("finishedAt", std::string()));
      // A pod can hold the history of more than one container, and a roll can leave two pods around:
      // the newest death is the one worth reporting.
      if (at < newestAt) continue;
      newest   = json{{"reason", reason},
                      {"exitCode", terminated.value("exitCode", 0)},
                      {"at", at},
                      {"restarts", terminated.value("restartCount", 0)}};
      newestAt = at;
    }
  }
  return newest;
}

// Why a pod's container is down right now: a failure with no running container to explain it away.
json NRoomRouter::PodFailure(const json & pod)
{
  const json statuses = pod.value("status", json::object()).value("containerStatuses", json::array());
  if (!statuses.is_array()) return json::object();

  json failure;
  long failureAt = -1;
  for (const auto & container : statuses) {
    if (!container.is_object()) continue;
    const json state = container.value("state", json::object());
    // It is running: whatever it did before is history, not a failure in progress.
    if (state.is_object() && state.contains("running")) continue;

    for (const char * where : {"state", "lastState"}) {
      const json terminated = ContainerTermination(container, where);
      const std::string reason = terminated.value("reason", std::string());
      if (!NRoomRouter::FailedReason(reason)) continue;

      const long at = NRoomRouter::Rfc3339(terminated.value("finishedAt", std::string()));
      if (at < failureAt) continue;
      failure   = json{{"reason", reason},
                       {"exitCode", terminated.value("exitCode", 0)},
                       {"at", at},
                       {"restarts", terminated.value("restartCount", 0)}};
      failureAt = at;
      break; // `state` first: it is the more recent of the two when a container is down
    }
  }
  return failure;
}

// The sentence a client shows for a termination: what happened, in the room's own terms.
std::string NRoomRouter::TerminationMessage(const std::string & reason, int exitCode)
{
  if (reason == "OOMKilled") return "the room was killed for using more memory than its limit";
  if (reason == "Evicted") return "the room's pod was evicted (its node was under pressure)";
  if (reason == "ContainerStatusUnknown") return "the room's container went away with its node";
  if (reason == "Error") return "the room's process exited with code " + std::to_string(exitCode);
  return "the room's container stopped: " + reason;
}

// What a client shows about a room's death, or nothing at all when the room has not died.
json NRoomRouter::LastErrorJson(const std::string & reason, int exitCode, long at, int restarts)
{
  if (reason.empty()) return json::object();
  return json{{"reason", reason},
              {"exitCode", exitCode},
              {"at", at},
              {"restarts", restarts},
              {"message", NRoomRouter::TerminationMessage(reason, exitCode)}};
}

// Why a room's container last died, read under the registry lock.
json NRoomRouter::RoomLastError(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it == fRooms.end()) return json::object();
  return NRoomRouter::LastErrorJson(it->second.lastReason, it->second.lastExit, it->second.lastAt,
                                    it->second.lastRestarts);
}

// The pods of one room, or every room's pods when the selector names no particular one.
std::string NRoomRouter::PodsPath(const std::string & labelSelector) const
{
  return "/api/v1/namespaces/" + fConfig.ns + "/pods?labelSelector=" + labelSelector;
}

// Remembers why a room's container died: in the registry, and on the room's Service.
//
// The registry is what makes it reportable while this process lives; the annotation is what makes it
// outlive the pod (a room that scales to zero takes its pod, and the pod's status with it). Neither
// is fatal when it fails: a room that cannot be annotated is still a room.
void NRoomRouter::NoteTermination(const std::string & name, const json & termination)
{
  if (!termination.is_object() || termination.empty()) return;

  const std::string reason = termination.value("reason", std::string());
  const long        at     = termination.value("at", 0L);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(name);
    if (it == fRooms.end()) return;
    // An older death never replaces a newer one, and a death already recorded is not written again:
    // every room/list would otherwise patch the Service.
    if (it->second.lastAt > 0 && at > 0 && at < it->second.lastAt) return;
    if (it->second.lastReason == reason && it->second.lastAt == at) return;
    it->second.lastReason   = reason;
    it->second.lastExit     = termination.value("exitCode", 0);
    it->second.lastAt       = at;
    it->second.lastRestarts = termination.value("restarts", 0);
  }

  std::string error;
  if (!Annotate(name, kLastErrorAnnotation, termination.dump(), error)) {
    NLogWarning("[room] cannot record why %s died: %s", name.c_str(), error.c_str());
  }
}

// A room's profile, read under the registry lock.
std::string NRoomRouter::RoomProfile(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  return it == fRooms.end() ? std::string() : it->second.profile;
}

// Records the profile a room is created with, under the registry lock.
void NRoomRouter::SetProfile(const std::string & name, const std::string & profile)
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it != fRooms.end()) it->second.profile = profile;
}

// A room's tokens, read under the registry lock.
json NRoomRouter::RoomAccess(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it == fRooms.end()) return json::object();
  return NRoomRouter::AccessJson(it->second.tokenRw, it->second.tokenRo);
}

// Whether the router's capture probe was already refused for this room at this revision, read
// under the registry lock.
bool NRoomRouter::CaptureRefused(const std::string & name, const std::string & revision) const
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  return it != fRooms.end() && !revision.empty() && it->second.captureRefusedAt == revision;
}

// Remember a refused capture probe, under the registry lock.
void NRoomRouter::NoteCaptureRefused(const std::string & name, const std::string & revision)
{
  std::lock_guard<std::mutex> lock(fMutex);
  const auto                  it = fRooms.find(name);
  if (it != fRooms.end()) it->second.captureRefusedAt = revision;
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

// The email a client asserts alongside its owner, when it sent one.
std::string NRoomRouter::RequestOwnerEmail(json & in)
{
  const json email = NdmspcRoomMember(in, "owner_email");
  if (email.is_string() && !email.get<std::string>().empty()) return email.get<std::string>();
  const json query = NdmspcRoomMember(in, "_query");
  if (query.is_string()) {
    const auto params = NRoomRouter::ParseQuery(query.get<std::string>());
    const auto it     = params.find("owner_email");
    if (it != params.end() && !it->second.empty()) return it->second;
  }
  return {};
}

// The profile a room/open asks for, when it names one.
//
// Read the way the owner is: the body first, then the query, so a link can carry the size a room is
// wanted at (`?profile=large`) as well as a script that posts it.
std::string NRoomRouter::RequestProfile(json & in)
{
  const json profile = NdmspcRoomMember(in, "profile");
  if (profile.is_string() && !profile.get<std::string>().empty()) return profile.get<std::string>();
  const json query = NdmspcRoomMember(in, "_query");
  if (query.is_string()) {
    const auto params = NRoomRouter::ParseQuery(query.get<std::string>());
    const auto it     = params.find("profile");
    if (it != params.end() && !it->second.empty()) return it->second;
  }
  return {};
}

// The caller of a request: what the server verified wins, and the client's own word is the fallback.
NRequestIdentity NRoomRouter::RequestIdentity(json & in)
{
  const NRequestIdentity identity = NRequestIdentity::FromJson(NdmspcRoomMember(in, "_identity"));
  if (identity.verified) return identity;
  // A client may say more than one thing about itself: a user name names what it creates, and the
  // email it adds lets an admin list written in emails recognise it.
  return NRequestIdentity::FromAssertion(NRoomRouter::RequestOwner(in), NRoomRouter::RequestOwnerEmail(in));
}

// The id a room gets when its creator is known: their name in front of it, so that two people can
// both have a room called "mine" without one of them taking the other's. The name is the owner's
// (their user name, as NRequestIdentity::Owner documents it), which keeps the id - a URL, a label
// and a resource name at once - short and free of an '@'.
std::string NRoomRouter::Qualify(const NRequestIdentity & identity, const std::string & id)
{
  const std::string owner = identity.Owner();
  if (owner.empty()) return id;
  return owner + "-" + id;
}

// The room a request names: an id that already names one is that room, and a new id is the caller's.
NRoomRouter::NRoomRef NRoomRouter::Resolve(const std::string & id, const NRequestIdentity & identity) const
{
  const std::string plain = NRoomRouter::RoomName(fConfig, id);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(plain);
    if (it != fRooms.end()) return {plain, it->second.value.empty() ? id : it->second.value};
  }
  const std::string qualified = NRoomRouter::Qualify(identity, id);
  return {NRoomRouter::RoomName(fConfig, qualified), qualified};
}

// What to tell a caller who asked for a room that is not theirs to use.
std::string NRoomRouter::NotOwnerMessage(const std::string & id, const std::string & owner)
{
  if (owner.empty()) return "Room '" + id + "' has no owner, so it is not yours to use";
  return "Room '" + id + "' belongs to " + owner;
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

// Creates the object, or merge-patches its spec and annotations when it already exists.
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
    patch["spec"] = object.value("spec", json::object());
    // The annotations travel with the spec, because they are part of what a room is: its size (see
    // kProfileAnnotation) has to survive a resize, so the next open - or a router that restarts -
    // still knows which profile the room was last asked for. Only the keys this object carries are
    // touched, so the session snapshot stored beside them is left alone.
    const json annotations = object.value("metadata", json::object()).value("annotations", json::object());
    if (annotations.is_object() && !annotations.empty()) {
      patch["metadata"]["annotations"] = annotations;
    }
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

// Stores something a room has to keep on its own Knative Service: its session snapshot, and why its
// container last died. An annotation is the place for state that has to outlive this process but
// belongs to the room rather than to the router.
bool NRoomRouter::Annotate(const std::string & name, const std::string & key, const std::string & value,
                           std::string & error)
{
  json patch;
  patch["metadata"]["annotations"][key] = value;

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
  if (!Annotate(name, kNRoomStateAnnotation, text, error)) {
    NLogWarning("[room] cannot store the session of %s: %s", name.c_str(), error.c_str());
  }
}

json NRoomRouter::ServiceObject(const NRoomConfig & cfg, const std::string & name, const std::string & value,
                                const std::string & stateUrl, const json & access, const std::string & owner,
                                const json & skeleton, const std::string & profile, const json & resources)
{
  json service;
  service["apiVersion"]                      = "serving.knative.dev/v1";
  service["kind"]                            = "Service";
  service["metadata"]["name"]                = name;
  service["metadata"]["namespace"]           = cfg.ns;
  service["metadata"]["labels"][kNRoomLabel] = NRoomRouter::Slug(value, 63);
  service["spec"]                            = skeleton.value("serviceSpec", json::object());

  // The label marks the object as a room and can hold only a slug of the id; the id itself - which
  // an email address qualifies, and a label value may not contain - travels in an annotation, so the
  // router can read the room back after a restart knowing exactly which id it was.
  service["metadata"]["annotations"][kRoomIdAnnotation] = value;

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

  // And its profile, for the same reason: the size a room was asked for is part of what it is, so a
  // restart (and every payload) still knows it without the caller repeating the choice.
  if (!profile.empty()) {
    service["metadata"]["annotations"][kProfileAnnotation] = profile;
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

  // What the room's profile allows it to use. The skeleton carries no resources of its own, so this
  // is where a room's size is decided - and re-deciding it (another profile on a later open) is what
  // resizes a room.
  if (resources.is_object() && !resources.empty()) {
    containers[0]["resources"] = resources;
  }

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
  route["metadata"]["labels"][kNRoomLabel] = NRoomRouter::Slug(value, 63);

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

// A room's pods, as the cluster answered them ({} when it did not).
json NRoomRouter::RoomPods(const std::string & name, const std::string & revision)
{
  // Knative labels each pod with its revision; before one exists, fall back to the room's service.
  const std::string selector = revision.empty() ? ("serving.knative.dev%2Fservice%3D" + name)
                                                : ("serving.knative.dev%2Frevision%3D" + revision);

  const auto response = Request("GET", PodsPath(selector));
  if (response.status != 200) return json::object();

  try {
    return json::parse(response.body);
  }
  catch (const std::exception &) {
    return json::object();
  }
}

// The scheduler's own words for a pod it cannot place.
//
// A Knative Service only ever reports "waiting for a Revision to become ready"; the reason lives on
// the pod ("0/1 nodes are available: 1 Insufficient cpu"), so that is where this looks. Returns
// kTRUE when a pod of that revision is reported unschedulable, with the message in reason.
bool NRoomRouter::PodUnschedulable(const json & pods, std::string & reason)
{
  // Bound to a name, like every list this file walks: `value()` returns a temporary, and a range-for
  // over one iterates memory that is already gone (it survives by luck, or segfaults).
  const json items = pods.value("items", json::array());
  for (const auto & pod : items) {
    const json conditions = pod.value("status", json::object()).value("conditions", json::array());
    for (const auto & condition : conditions) {
      if (condition.value("type", "") != "PodScheduled" || condition.value("status", "") != "False") continue;
      if (condition.value("reason", "") != "Unschedulable") continue;
      reason = condition.value("message", "the cluster cannot place the room's pod");
      return true;
    }
  }
  return false;
}

bool NRoomRouter::WaitReady(const std::string & name, std::string & revision, std::string & error,
                                std::string & code)
{
  const long  deadline = NdmspcRoomNow() + fConfig.readyTimeoutSec;
  std::string lastMessage;
  std::string lastUnschedulable; // the scheduler's verdict, and how often in a row it has said it
  int         unschedulableSeen = 0;
  std::string lastFailure;       // why its container keeps dying, and how often in a row
  int         failureSeen = 0;

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

        // A pod that is not becoming ready says why in its own two ways, and both are worth more than
        // the Service's silence: the scheduler refusing to place it, and a container that keeps
        // dying. Either verdict has to repeat, so a race while another room scales up, or a container
        // that is merely restarting, cannot fail a room.
        const json  pods = RoomPods(name, created);
        std::string unschedulable;
        if (PodUnschedulable(pods, unschedulable)) {
          if (unschedulable == lastUnschedulable) {
            ++unschedulableSeen;
          }
          else {
            lastUnschedulable = unschedulable;
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

        // A container the kernel killed - the room outgrew its profile - leaves nothing but this.
        const json roomPods = pods.value("items", json::array());
        json       failure;
        json       failurePod;
        for (const auto & pod : roomPods) {
          const json found = NRoomRouter::PodFailure(pod);
          if (found.empty()) continue;
          failure    = found;
          failurePod = pod;
          break;
        }
        if (!failure.empty()) {
          // Remember it whether or not the creation fails: a client that is not waiting for this
          // room still deserves to see why it cannot come up.
          NoteTermination(name, NRoomRouter::PodTermination(failurePod));
          const std::string reason = failure.value("reason", std::string());
          if (reason == lastFailure) {
            ++failureSeen;
          }
          else {
            lastFailure = reason;
            failureSeen = 1;
          }
          if (failureSeen >= 2) {
            error = NRoomRouter::TerminationMessage(reason, failure.value("exitCode", 0));
            code  = NRoomRouter::kContainerError;
            return false;
          }
        }
        else {
          lastFailure.clear();
          failureSeen = 0;
        }

        const json conditions = status.value("conditions", json::array());
        for (const auto & condition : conditions) {
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
// Every action that reads the registry adopts first (list, status, close,
// backup, restore, open): the registry is a cache of the cluster, and an action
// answering from a cache that has not been filled reports the rooms that exist
// as if they did not - an empty list, an untracked room, an empty backup.
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
          // The id is in the annotation; the label carries only its slug, because a label value
          // cannot hold the '@' of the email an id may be qualified with. A room created before that
          // annotation existed has the id in the label itself, which is what this falls back to.
          const json annotations = metadata.value("annotations", json::object());
          state.value            = annotations.value(kRoomIdAnnotation, "");
          if (state.value.empty()) {
            state.value = metadata.value("labels", json::object()).value(kNRoomLabel, "");
          }
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
        if (state.profile.empty()) {
          // And its size: a room keeps the profile it was created with, so opening it again does not
          // silently resize it. A room created before profiles existed has none, and takes the
          // skeleton's default the next time it is opened.
          state.profile = metadata.value("annotations", json::object()).value(kProfileAnnotation, "");
        }
        if (state.lastReason.empty()) {
          // And why it died last: the pod that said so is long gone (which is what the annotation is
          // for), and a room that comes back should still be able to tell a client that its
          // predecessor ran out of memory. An annotation that cannot be read is one this router does
          // not have, not a reason to fail the adoption.
          try {
            const json reason = json::parse(
                metadata.value("annotations", json::object()).value(kLastErrorAnnotation, ""));
            state.lastReason   = reason.value("reason", std::string());
            state.lastExit     = reason.value("exitCode", 0);
            state.lastAt       = reason.value("at", 0L);
            state.lastRestarts = reason.value("restarts", 0);
          }
          catch (const std::exception &) {
          }
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

  Ndmspc::NHttpRequest        http;
  std::string                 error;
  Ndmspc::NRoomSession::State state    = Ndmspc::NRoomSession::State::Unreachable;
  const json                  access   = RoomAccess(name);
  const json                  snapshot = Ndmspc::NRoomSession::Capture(
      http, baseUrl, value, error, access.value(NRoomAccess::kReadWrite, ""), &state);
  if (snapshot.is_null()) {
    // NRoomSession reports nothing for a room that has no file open, so a freshly
    // started pod can never overwrite a good snapshot with emptiness.
    if (!error.empty()) {
      // A refusal is the expected answer from a room that authenticates /api - this component is
      // internal and has no user token to present - so it is said once at information level, and
      // then the room is not asked again at this revision. Anything else (a room that cannot be
      // reached, an answer that cannot be read) is a real failure and stays a warning.
      if (state == Ndmspc::NRoomSession::State::Refused) {
        NLogInfo("[room] cannot capture the session of %s: %s", name.c_str(), error.c_str());
        NoteCaptureRefused(name, revision);
      }
      else {
        NLogWarning("[room] cannot capture the session of %s: %s", name.c_str(), error.c_str());
      }
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
  if (!Annotate(name, kNRoomStateAnnotation, text, patchError)) {
    NLogWarning("[room] cannot store the session of %s: %s", name.c_str(), patchError.c_str());
  }
}

// Captures a room's session off the request path (see CaptureNow).
void NRoomRouter::Capture(const std::string & name, const std::string & value, const std::string & revision)
{
  if (RoomBaseUrl(revision).empty()) return;
  // Already refused at this revision (see CaptureNow): asking again would repeat the same refusal
  // on every room/list. A new revision is a room that may now accept the probe.
  if (CaptureRefused(name, revision)) return;
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
bool NRoomRouter::EnsureWorker(const std::string & value, int generation, const std::string & requestedProfile,
                                   Replay replay, json & payload, std::string & error, std::string & code)
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

  // The room's size: what this open asked for, or - on an open that names none - whatever the room
  // already runs, so re-opening a room never resizes it behind the caller's back. Only a room that
  // has no profile of its own takes the skeleton's default.
  std::string       profileError;
  const std::string wanted   = requestedProfile.empty() ? RoomProfile(name) : requestedProfile;
  const std::string profile  = NRoomRouter::ProfileName(skeleton, wanted, profileError);
  if (!profileError.empty()) return fail(profileError, kUnknownProfile);
  const json resources = NRoomRouter::ProfileResources(skeleton, profile, profileError);
  if (!profileError.empty()) return fail(profileError, kUnknownProfile);
  SetProfile(name, profile);

  const json access  = RoomAccess(name);
  const json service = NRoomRouter::ServiceObject(cfg, name, value, RouterBaseUrl(), access, RoomOwner(name),
                                                 skeleton, profile, resources);
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
  if (!profile.empty()) payload["profile"] = profile;
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
bool NRoomRouter::EnsureStart(const std::string & value, const std::string & profile, bool wait, Replay replay,
                                  json & payload, std::string & error, std::string & code)
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
      // What this open asked for, so a client can say what the room is being created as.
      if (!profile.empty()) payload["profile"] = profile;
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

  if (wait) return EnsureWorker(value, generation, profile, replay, payload, error, code);

  payload["room"]      = value;
  payload["name"]      = name;
  payload["param"]     = cfg.param;
  payload["url"]       = NRoomRouter::ClientUrl(cfg, value, tokenRw);
  payload["access"]    = NRoomRouter::AccessJson(tokenRw, tokenRo);
  if (const std::string owner = RoomOwner(name); !owner.empty()) payload["owner"] = owner;
  if (!profile.empty()) payload["profile"] = profile;
  payload["ttl"]       = cfg.idleTtlSec;
  payload["state"]     = "preparing";
  payload["phase"]     = "service";
  payload["startedAt"] = startedAt;
  NLogInfo("[room] creating '%s' in the background", value.c_str());

  // The worker owns the room from here: it publishes the phase and the outcome on the registry
  // entry, which is what room/list and room/status report to every other client.
  ReapWorkers();
  auto done = std::make_shared<std::atomic<bool>>(false);
  SpawnWorker(value, std::thread([this, value, generation, profile, replay, done]() {
                json        result;
                std::string failure;
                std::string code;
                try {
                  if (!EnsureWorker(value, generation, profile, replay, result, failure, code) &&
                      !failure.empty()) {
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
                     "room/status or room/list for the outcome. 'profile' picks one of the room "
                     "skeleton's sizes (see room/list for the ones this deployment offers); without "
                     "it a room keeps the size it already has, and a new one takes the skeleton's "
                     "default.",
      .methods     = {"GET", "POST"},
      .inputSchema = {{"properties",
                       {{"room", {{"type", "string"}, {"description", "Room id (any client-chosen string)."}}},
                        {"profile",
                         {{"type", "string"},
                          {"description", "Room skeleton profile to create (or resize) the room at, e.g. "
                                          "small; defaults to the room's own, or the skeleton's "
                                          "defaultProfile."}}},
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
  Ndmspc::RegisterMcpTool("room/capacity", {
      .description = "Report the cluster's capacity for rooms: what its nodes have (allocatable), what "
                     "the rooms and everything else already reserve (requests), and what is left "
                     "(free, plus a per-node breakdown). These are reservations, not live usage - the "
                     "same numbers the scheduler weighs, which is what decides whether another room "
                     "can start. Needs read access to nodes and pods cluster-wide: without it the "
                     "answer carries complete=false and only the parts it could read.",
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
  //  /api/room/capacity — what the cluster has, and what the rooms reserve
  // -------------------------------------------------------------------------
  handlers["room/capacity"] = [](std::string method, json & in, json & out, json & /*wsOut*/,
                                 std::map<std::string, TObject *> &) {
    NRoomRouter::Instance().HandleCapacity(method, in, out);
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

  // A room belongs to whoever creates it, and room/open is the call that creates one - so this is
  // where both halves of that rule are applied. The id names the room: one that already exists is
  // that room (and is refused to a caller it does not belong to, since this call answers with the
  // room's own link), while an id that names nothing yet becomes the caller's own - which is what
  // lets two people both have a room called "mine", one named after each of them.
  const NRequestIdentity identity = RequestIdentity(in);
  const NRoomRef         ref     = Resolve(id, identity);
  bool                   created = false;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(ref.name);
    if (it == fRooms.end()) {
      created = true;
      if (!identity.Empty()) {
        NRoomState & state = fRooms[ref.name];
        state.name         = ref.name;
        state.value        = ref.value;
        state.owner        = identity.Owner();
      }
    }
    else if (!MaySee(it->second, identity)) {
      out["result"] = "failure";
      out["code"]   = kNotOwner;
      out["error"]  = NRoomRouter::NotOwnerMessage(ref.value, it->second.owner);
      return;
    }
  }

  // Creating a room is slow and the router serves one request at a time, so the caller decides
  // whether to wait for it (the default, which is what scripts relied on) or to be told that it
  // is being prepared and poll room/status: the work then runs off the request path, which keeps
  // the router answering everyone else and lets several rooms be prepared at once. The session
  // replay - the call that wakes the room - happens inside that work either way.
  // The size the caller asks for, if any: resolved against the skeleton while the room is created,
  // so a name this deployment does not offer comes back as a failure with `unknown_profile`.
  const std::string profile = NRoomRouter::RequestProfile(in);

  const bool wait = NRoomRouter::WaitFlag(fConfig, in);
  json        payload;
  std::string error;
  std::string code;
  bool        started = false;
  try {
    started = EnsureStart(ref.value, profile, wait, Replay::Stored, payload, error, code);
  }
  catch (const std::exception & e) {
    // A failure here must not take the router down with it: report it like any other.
    error   = std::string("cannot create the room: ") + e.what();
    code.clear();
    started = false;
  }
  if (!started) {
    NLogError("[room] open failed for '%s': %s", ref.value.c_str(), error.c_str());
    out["result"] = "failure";
    out["error"]  = error;
    if (!code.empty()) out["code"] = code;
    // A creation that failed because the room's container keeps dying says so in its own terms: the
    // reason is what tells a user to give the room a bigger profile.
    const json lastError = RoomLastError(ref.name);
    if (!lastError.empty()) out["payload"]["lastError"] = lastError;
    return;
  }
  if (payload.value("state", std::string()) == "preparing") {
    NLogInfo("[room] room '%s' is being prepared (%s)", ref.value.c_str(), payload.value("phase", "").c_str());
  }
  else {
    NLogInfo("[room] room '%s' %s (%s)", ref.value.c_str(), created ? "created" : "already there",
             payload.value("revision", "").c_str());
  }

  // Whether this call made the room or found it: the answer is the same either way - room/open is
  // ensure, which is what every client relies on - but a client that just pressed "create" may want
  // to say which of the two happened.
  payload["created"] = created;

  // What happened to the room last time, when anything did: a client that is opening a room back up
  // is exactly the client that wants to know its predecessor ran out of memory.
  const json lastError = RoomLastError(ref.name);
  if (!lastError.empty()) payload["lastError"] = lastError;

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

  // Which room this id names, and whose it is, are both read from the registry - so a router that has
  // just started has to read the cluster back first, or it reports someone's room as untracked.
  Adopt();

  const NRequestIdentity identity = RequestIdentity(in);
  const NRoomRef         ref      = Resolve(id, identity);
  Touch(ref.name);

  // Somebody else's room is not this caller's to look at: its answer carries the room's links and
  // the session it is in.
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(ref.name);
    if (it != fRooms.end() && !MaySee(it->second, identity)) {
      out["result"] = "failure";
      out["code"]   = kNotOwner;
      out["error"]  = NRoomRouter::NotOwnerMessage(ref.value, it->second.owner);
      return;
    }
  }

  // Why its container last died, while the cluster still remembers: a room that is not serving has
  // no other witness, and this is the call a client makes when it wants to know what happened.
  const json itsPods = RoomPods(ref.name, "").value("items", json::array());
  for (const auto & pod : itsPods) {
    const json termination = NRoomRouter::PodTermination(pod);
    if (termination.empty()) continue;
    NoteTermination(ref.name, termination);
    break;
  }

  out["result"]       = "success";
  out["payload"]["room"]     = ref.value;
  out["payload"]["name"]     = ref.name;
  out["payload"]["param"]    = fConfig.param;

  // What its creation is doing, if anything: a room is registered as soon as its creation
  // starts, so a client can say what it is waiting for rather than only that it is not ready.
  std::string state;
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(ref.name);
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
      if (!it->second.profile.empty()) out["payload"]["profile"] = it->second.profile;
      const json lastError = NRoomRouter::LastErrorJson(it->second.lastReason, it->second.lastExit,
                                                        it->second.lastAt, it->second.lastRestarts);
      if (!lastError.empty()) out["payload"]["lastError"] = lastError;
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
  const auto response = Request("GET", SvcPath(ref.name));
  out["payload"]["exists"] = (response.status == 200);
  if (response.status == 200) {
    try {
      const json svc    = json::parse(response.body);
      const json status = svc.value("status", json::object());
      bool       ready  = false;
      const json conditions = status.value("conditions", json::array());
      for (const auto & condition : conditions) {
        if (condition.value("type", "") == "Ready") ready = (condition.value("status", "") == "True");
      }
      out["payload"]["ready"]    = ready;
      out["payload"]["revision"] = status.value("latestReadyRevisionName", "");
      if (state.empty()) state   = ready ? "ready" : "not ready";

      // What the room may use (see `ServiceResources`): the detail pane asks for it here, and the
      // list reports the same numbers for every room.
      const json resources = NRoomRouter::ServiceResources(svc);
      if (!resources.empty()) out["payload"]["resources"] = resources;
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

  // The registry is process-local, so a router that has just started holds nothing while the cluster
  // still has the rooms. Adopt before answering: otherwise the list comes back empty - a client sees
  // no rooms at all - until something opens one, which is exactly when adoption used to happen.
  Adopt();

  // Who is asking decides what the list holds: see "Ownership and visibility".
  const NRequestIdentity identity = RequestIdentity(in);

  // Why the rooms' containers last died: one pods list for the whole namespace, matched to rooms by
  // the Knative label that carries their Service name. This happens before the registry is read, so
  // a note it finds is part of what this answer reports, and it is one API call however many rooms
  // there are. The pod is deleted when a room scales to zero, which is why what it reports here is
  // remembered rather than looked up again later.
  {
    const auto pods = Request("GET", PodsPath("serving.knative.dev%2Fservice"));
    if (pods.status == 200) {
      std::map<std::string, json> newest;
      try {
        const json allPods = json::parse(pods.body).value("items", json::array());
        for (const auto & pod : allPods) {
          const std::string service = pod.value("metadata", json::object())
                                          .value("labels", json::object())
                                          .value("serving.knative.dev/service", std::string());
          if (service.empty()) continue;
          const json termination = NRoomRouter::PodTermination(pod);
          if (termination.empty()) continue;
          // A roll leaves two pods around: the later death is the one worth keeping.
          const auto seen = newest.find(service);
          if (seen == newest.end() || termination.value("at", 0L) > seen->second.value("at", 0L)) {
            newest[service] = termination;
          }
        }
      }
      catch (const std::exception &) {
      }
      for (const auto & entry : newest) NoteTermination(entry.first, entry.second);
    }
  }

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
      if (!entry.second.profile.empty()) room["profile"] = entry.second.profile;
      // Why its container last died, if it ever did: the reason a room the user expected to be
      // serving is not, which Kubernetes otherwise keeps to itself.
      const json lastError = NRoomRouter::LastErrorJson(entry.second.lastReason, entry.second.lastExit,
                                                        entry.second.lastAt, entry.second.lastRestarts);
      if (!lastError.empty()) room["lastError"] = lastError;
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
      const json conditions = status.value("conditions", json::array());
      for (const auto & condition : conditions) {
        if (condition.value("type", "") == "Ready") ready = (condition.value("status", "") == "True");
      }
      room["ready"]    = ready;
      room["revision"] = status.value("latestReadyRevisionName", room.value("revision", ""));
      room["state"]    = ready ? "ready" : "not ready";

      // What the room may use, from the Service the router itself stamped: known while the room is
      // scaled to zero, and absent for a room whose skeleton declares nothing.
      const json resources = NRoomRouter::ServiceResources(svc);
      if (!resources.empty()) room["resources"] = resources;

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
  // Whether this caller was treated as an admin. A view can then say why the list is wider than the
  // caller's own rooms - or that it is not - without keeping a second copy of the admin list that
  // could drift from this one.
  out["payload"]["admin"] = IsAdmin(identity);

  // The sizes this deployment offers, so a client can let a user choose one without knowing the
  // names: the same skeleton the rooms are created from. A skeleton without profiles - a deployment
  // that offers no sizes - simply reports none, and the choice is not shown at all.
  json skeleton;
  std::string skeletonError;
  if (Skeleton(skeleton, skeletonError)) {
    const json profiles = skeleton.value("profiles", json::object());
    if (profiles.is_object() && !profiles.empty()) {
      out["payload"]["profiles"]       = profiles;
      out["payload"]["defaultProfile"] = skeleton.value("defaultProfile", std::string());
    }
  }
  else {
    NLogWarning("[room] cannot report the room profiles: %s", skeletonError.c_str());
  }
}

// ===========================================================================
//  room/capacity
// ===========================================================================
namespace {
/// @brief Adds what a set of containers declares on one side: `requests` (what they reserve) or
///        `limits` (the most they may reach).
///
/// A container that declares nothing on that side contributes nothing, and a quantity nobody can read
/// is counted as nothing: the answer reports what was asked for, never a guess.
void AddResources(long & cpuMillis, long & bytes, const json & containers, const char * side)
{
  if (!containers.is_array()) return;
  for (const auto & container : containers) {
    if (!container.is_object()) continue;
    const json declared = container.value("resources", json::object()).value(side, json::object());
    if (!declared.is_object()) continue;
    cpuMillis += NRoomRouter::CpuMillis(declared.value("cpu", std::string()));
    bytes += NRoomRouter::Bytes(declared.value("memory", std::string()));
  }
}

/// @brief The amounts every figure in room/capacity is written as, in their base units.
json Amounts(long cpuMillis, long bytes)
{
  return json{{"cpuMillis", cpuMillis}, {"memBytes", bytes}};
}
} // namespace

void NRoomRouter::HandleCapacity(const std::string & method, json & in, json & out)
{
  (void)in;
  if (method.find("GET") == std::string::npos) {
    out["result"] = "failure";
    out["error"]  = "Unsupported HTTP method for room/capacity";
    return;
  }

  // What the cluster has to give: `allocatable` is the number the scheduler counts, and the ceiling
  // a room's requests have to fit under.
  bool       nodesRead = false;
  long       allocCpu  = 0;
  long       allocMem  = 0;
  json       perNode   = json::array();
  const auto nodes     = Request("GET", "/api/v1/nodes");
  if (nodes.status == 200) {
    try {
      const json items = json::parse(nodes.body).value("items", json::array());
      for (const auto & node : items) {
        const json allocatable = node.value("status", json::object()).value("allocatable", json::object());
        const long cpu         = NRoomRouter::CpuMillis(allocatable.value("cpu", std::string()));
        const long mem         = NRoomRouter::Bytes(allocatable.value("memory", std::string()));
        allocCpu += cpu;
        allocMem += mem;
        perNode.push_back(json{{"name", node.value("metadata", json::object()).value("name", "")},
                               {"allocatable", Amounts(cpu, mem)}});
      }
      nodesRead = true;
    }
    catch (const std::exception &) {
      nodesRead = false;
      allocCpu  = 0;
      allocMem  = 0;
      perNode   = json::array();
    }
  }

  // What is already spoken for: every pod, not only this namespace's, because a room lands wherever
  // the scheduler finds room and everything else on the cluster is competing for the same nodes.
  bool        podsRead = false;
  long        reqCpu   = 0;
  long        reqMem   = 0;
  long        limCpu   = 0;
  long        limMem   = 0;
  long        roomCpu  = 0;
  long        roomMem  = 0;
  long        roomLimCpu = 0;
  long        roomLimMem = 0;
  json        byNode   = json::object();
  std::map<std::string, bool> roomServices;
  const auto  pods = Request("GET", "/api/v1/pods");
  if (pods.status == 200) {
    try {
      const json clusterPods = json::parse(pods.body).value("items", json::array());
      for (const auto & pod : clusterPods) {
        // A pod that has finished holds nothing: counting it would inflate what is reserved.
        const std::string phase = pod.value("status", json::object()).value("phase", std::string());
        if (phase == "Succeeded" || phase == "Failed") continue;

        const json spec = pod.value("spec", json::object());
        long       cpu  = 0;
        long       mem  = 0;
        AddResources(cpu, mem, spec.value("containers", json::array()), "requests");
        reqCpu += cpu;
        reqMem += mem;

        // And the other side of the same declaration: what the pods may reach. Reservations decide
        // whether a room starts, ceilings decide how much it can take once it has.
        long limCpuPerPod = 0;
        long limMemPerPod = 0;
        AddResources(limCpuPerPod, limMemPerPod, spec.value("containers", json::array()), "limits");
        limCpu += limCpuPerPod;
        limMem += limMemPerPod;

        const std::string node = spec.value("nodeName", std::string());
        if (!node.empty()) {
          const json held = byNode.value(node, json::object());
          // Both sides per node: what it reserves, and what it may reach. The second is what a fit by
          // ceilings has to subtract, the way the first is what a fit by reservations subtracts.
          byNode[node] = json{{"cpuMillis", held.value("cpuMillis", 0L) + cpu},
                              {"memBytes", held.value("memBytes", 0L) + mem},
                              {"limCpuMillis", held.value("limCpuMillis", 0L) + limCpuPerPod},
                              {"limMemBytes", held.value("limMemBytes", 0L) + limMemPerPod}};
        }

        // The label Knative puts on a room's pods (and on the sidecar beside them): what the rooms
        // reserve, sidecars included - which is what a room really costs the cluster. The entry
        // Service is a Knative Service too, so a pod only counts as a room when the router named it
        // like one: counting the router itself would be a lie about what the rooms cost.
        const std::string service =
            pod.value("metadata", json::object()).value("labels", json::object()).value("serving.knative.dev/service", std::string());
        if (!service.empty() && service.rfind(fConfig.prefix, 0) == 0) {
          roomCpu += cpu;
          roomMem += mem;
          roomLimCpu += limCpuPerPod;
          roomLimMem += limMemPerPod;
          roomServices[service] = true;
        }
      }
      podsRead = true;
    }
    catch (const std::exception &) {
      podsRead = false;
      reqCpu   = 0;
      reqMem   = 0;
      roomCpu  = 0;
      roomMem  = 0;
      byNode   = json::object();
    }
  }

  out["result"] = "success";
  if (nodesRead) {
    out["payload"]["nodes"]       = static_cast<int>(perNode.size());
    out["payload"]["allocatable"] = Amounts(allocCpu, allocMem);
  }
  if (podsRead) {
    // Both sides of what the cluster has been promised: what the pods reserve, and the most they may
    // reach. A client can show either; `free` below is about reservations, because that is what the
    // scheduler places.
    out["payload"]["requests"] = Amounts(reqCpu, reqMem);
    out["payload"]["limits"]   = Amounts(limCpu, limMem);
    out["payload"]["rooms"]    = json{{"count", static_cast<int>(roomServices.size())},
                                      {"requests", Amounts(roomCpu, roomMem)},
                                      {"limits", Amounts(roomLimCpu, roomLimMem)}};
    out["payload"]["other"]    = json{{"requests", Amounts(reqCpu - roomCpu, reqMem - roomMem)},
                                      {"limits", Amounts(limCpu - roomLimCpu, limMem - roomLimMem)}};
  }
  if (nodesRead && podsRead) {
    out["payload"]["free"] = Amounts(std::max(0L, allocCpu - reqCpu), std::max(0L, allocMem - reqMem));

    // Per node too, because what is free across the cluster is not what one room can use: 4 GiB free
    // spread as 2 + 2 starts nothing, and only the biggest node's free predicts that.
    for (auto & node : perNode) {
      const std::string name = node.value("name", std::string());
      const json        used = byNode.value(name, json::object());
      const long cpu = used.value("cpuMillis", 0L);
      const long mem = used.value("memBytes", 0L);
      node["requests"] = Amounts(cpu, mem);
      node["free"]     = Amounts(std::max(0L, node["allocatable"].value("cpuMillis", 0L) - cpu),
                                 std::max(0L, node["allocatable"].value("memBytes", 0L) - mem));
    }
    out["payload"]["perNode"] = perNode;

    // How many more rooms of each profile could still start. A room has to fit on *one* node, so this
    // is the biggest free node against what a profile asks for - and the tighter of a profile's two
    // requests is what limits it, which is what a user needs in order to choose a size.
    long bestCpu = 0;
    long bestMem = 0;
    long bestLimCpu = 0;
    long bestLimMem = 0;
    for (const auto & node : perNode) {
      bestCpu = std::max(bestCpu, node["free"].value("cpuMillis", 0L));
      bestMem = std::max(bestMem, node["free"].value("memBytes", 0L));

      // The same node's headroom by ceilings: what it can give minus the limits its pods declare. A
      // room that never reaches its limit costs less than this says, which is what "if every room
      // peaked at once" means.
      const json held = byNode.value(node.value("name", std::string()), json::object());
      bestLimCpu = std::max(bestLimCpu, std::max(0L, node["allocatable"].value("cpuMillis", 0L) -
                                                          held.value("limCpuMillis", 0L)));
      bestLimMem = std::max(bestLimMem, std::max(0L, node["allocatable"].value("memBytes", 0L) -
                                                          held.value("limMemBytes", 0L)));
    }

    json        skeleton;
    std::string skeletonError;
    json        fits = json::object();
    if (Skeleton(skeleton, skeletonError)) {
      json byRequests = json::object();
      json byLimits   = json::object();
      // Bound to a name: `value()` returns a temporary, and a range-for over one iterates memory that
      // is already gone - it survives by luck in a test and segfaults in a release build.
      const json offered = skeleton.value("profiles", json::object());
      for (const auto & profile : offered.items()) {
        std::string profileError;
        const json  declared = NRoomRouter::ProfileResources(skeleton, profile.key(), profileError);

        // Each side against what it costs on that side: a room's reservations against the node's
        // reservations-headroom, its ceilings against the node's ceilings-headroom.
        const auto fit = [](long freeCpu, long freeMem, const json & ask) {
          const long cpu = NRoomRouter::CpuMillis(ask.value("cpu", std::string()));
          const long mem = NRoomRouter::Bytes(ask.value("memory", std::string()));
          if (cpu <= 0 || mem <= 0) return json::object(); // a size that asks for nothing constrains nothing

          const long byCpu = freeCpu / cpu;
          const long byMem = freeMem / mem;
          return json{{"count", std::min(byCpu, byMem)}, {"limitedBy", byCpu <= byMem ? "cpu" : "memory"}};
        };

        const json requested = fit(bestCpu, bestMem, declared.value("requests", json::object()));
        if (!requested.empty()) byRequests[profile.key()] = requested;
        const json limited = fit(bestLimCpu, bestLimMem, declared.value("limits", json::object()));
        if (!limited.empty()) byLimits[profile.key()] = limited;
      }

      if (!byRequests.empty() || !byLimits.empty()) {
        fits = json{{"requests", byRequests}, {"limits", byLimits}};
      }
    }
    if (!fits.empty()) out["payload"]["fits"] = fits;
  }

  // Whether these numbers cover the whole cluster. They are gathered from what this router is allowed
  // to read, so a missing permission answers with less rather than with something invented - the same
  // rule that keeps a room working when pods cannot be listed.
  out["payload"]["complete"] = nodesRead && podsRead;
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

  // Deleting someone else's room is not this caller's to do: adopt first, because a router that has
  // just started has not read the cluster back yet and would take an unread room for nobody's.
  Adopt();

  const NRequestIdentity identity = RequestIdentity(in);
  const NRoomRef         ref      = Resolve(id, identity);
  {
    std::lock_guard<std::mutex> lock(fMutex);
    const auto                  it = fRooms.find(ref.name);
    if (it != fRooms.end() && !MaySee(it->second, identity)) {
      out["result"] = "failure";
      out["code"]   = kNotOwner;
      out["error"]  = NRoomRouter::NotOwnerMessage(ref.value, it->second.owner);
      return;
    }
  }

  CloseRoom(ref.value); // a room that is being created is cancelled on the way out
  out["result"]  = "success";
  out["payload"]["room"] = ref.value;
  out["payload"]["name"] = ref.name;
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

  // The room's own credential, not a user's. This channel is internal - the room reports its
  // session and fetches it back - so the caller is the room itself, and the read-write token it
  // was created with is what says so. A room the router does not know (a report that arrives
  // before the router has adopted it after a restart) and one created without tokens are let
  // through as they always were: there is nothing to check those against.
  const json        query     = NdmspcRoomMember(in, "_query");
  const std::string presented = query.is_string() ? NRoomAccess::TokenFromQuery(query.get<std::string>()) : std::string();
  const json        access    = RoomAccess(name);
  const std::string expected  = access.is_object() ? access.value(NRoomAccess::kReadWrite, std::string()) : std::string();
  if (!expected.empty() && presented != expected) {
    out["result"] = "failure";
    out["error"]  = presented.empty() ? "The room's access token is required to report or fetch its session"
                                      : "The room's access token does not match this room";
    NLogWarning("[room] refused a session request for %s: the room's access token %s", name.c_str(),
                presented.empty() ? "is missing" : "does not match");
    return;
  }

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

  // What is exported is what the registry holds: adopt first, or a router that has just started
  // exports an empty document - and the rooms it is supposed to back up look like they never existed.
  Adopt();

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

  // Every room in the document is matched against the registry - to refuse one that belongs to
  // someone else, and to carry the owner and tokens the room already has. Adopt first, so a router
  // that has just started recognises the rooms that are already there instead of blanking them.
  Adopt();

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
        refusal["error"] = NRoomRouter::NotOwnerMessage(id, it->second.owner);
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
      // No profile: a restored room is re-created from today's skeleton, so it takes the skeleton's
      // default unless it already had a size of its own.
      ensured = EnsureStart(id, /*profile=*/std::string(), /*wait=*/true, Replay::None, payload, error, code);
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

