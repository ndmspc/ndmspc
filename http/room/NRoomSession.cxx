#include "NRoomSession.h"

#include <cstddef>
#include <ctime>
#include <exception>
#include <map>
#include <string>
#include <utility>

#include "ndmspc/core/NUtils.h"

#include "NRoomAccess.h"

namespace Ndmspc {

namespace {

/// @brief The actions that define session state, and are therefore replayed.
constexpr const char * kOpenRoute    = "ngnt/open";
constexpr const char * kReshapeRoute = "ngnt/reshape";

/// @brief Read a member without throwing when it is absent or of another type.
json Member(const json & object, const char * key)
{
  if (!object.is_object() || !object.contains(key)) return json();
  return object[key];
}

/// @brief Read a string member without throwing.
std::string StringMember(const json & object, const char * key)
{
  return NUtils::GetJsonString(Member(object, key));
}

/// @brief Shorten a body so a whole response cannot flood an error message.
std::string Elide(const std::string & text, std::size_t maxLength = 200)
{
  if (text.size() <= maxLength) return text;
  return text.substr(0, maxLength) + "...";
}

/// @brief Normalize a room base URL (no trailing slash).
std::string TrimBase(std::string url)
{
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

/// @brief GET a room endpoint and parse the response.
///
/// A room that was given access tokens refuses a request that carries none, so the router passes
/// its read-write token along.
bool GetJson(NHttpRequest & http, const std::string & url, json & out, std::string & error,
             const std::string & token = std::string())
{
  NHttpResponse response;
  try {
    std::map<std::string, std::string> headers;
    if (!token.empty()) headers[NRoomAccess::kHeader] = token;
    response = http.request("GET", url, "", headers);
  }
  catch (const std::exception & e) {
    error = "cannot reach the room at " + url + " (" + e.what() + ")";
    return false;
  }

  if (response.status != 200) {
    error = "the room at " + url + " answered HTTP " + std::to_string(response.status);
    return false;
  }

  try {
    out = json::parse(response.body);
  }
  catch (const json::parse_error &) {
    error = "the room at " + url + " returned a response that is not JSON: " + Elide(response.body);
    return false;
  }
  return true;
}

/// @brief Read the drill-down state point a room currently holds.
bool ReadPoint(NHttpRequest & http, const std::string & base, json & point, std::string & error,
               const std::string & token = std::string())
{
  json stateDoc;
  if (!GetJson(http, base + "/api/state", stateDoc, error, token)) return false;

  const json spectra = Member(Member(Member(stateDoc, "payload"), "metadata"), "spectra");
  point              = Member(spectra, "point");
  return true;
}

} // namespace

bool NRoomSession::IsReplayable(const std::string & routeName)
{
  return routeName == kOpenRoute || routeName == kReshapeRoute;
}

NRoomSession::State NRoomSession::Probe(NHttpRequest & http, const std::string & roomBaseUrl, std::string & file,
                                        std::string & error, const std::string & token)
{
  const std::string url = TrimBase(roomBaseUrl) + "/api/" + kOpenRoute;

  json response;
  if (!GetJson(http, url, response, error, token)) return State::Unreachable;

  // An empty room answers with a failure here ("File ... not opened"), so only a
  // successful response that actually carries a file name counts as an active session.
  const std::string name = StringMember(response, "file");
  if (StringMember(response, "result") == "success" && !name.empty()) {
    file = name;
    return State::Active;
  }

  // A refusal is not an empty room: the room answered that it will not serve this caller, because
  // it wants a credential the caller did not present - a user bearer token when the deployment
  // authenticates /api, or its own token when the one presented is stale. The embedded ROOT
  // server cannot set an error status, so this arrives as HTTP 200 with the reason in the JSON
  // envelope; saying so keeps a capture that cannot happen from looking like a room with nothing
  // open. Both envelope shapes the server uses are read: {"error":{"code":...}} from the bearer
  // gate and {"result":"failure","code":...} from a room's own access gate.
  std::string code = StringMember(Member(response, "error"), "code");
  if (code.empty()) code = StringMember(response, "code");
  if (!code.empty()) {
    error = "the room refused the request (" + code + "); the router cannot read its session";
    return State::Refused;
  }
  return State::Empty;
}

json NRoomSession::Build(const std::string & roomId, const std::string & file, const json & history,
                         const json & point)
{
  // No file open means no session, and a snapshot of nothing must never be stored.
  if (file.empty()) return json();

  json snapshot;
  snapshot["v"]    = 1;
  snapshot["room"] = roomId;
  snapshot["at"]   = static_cast<long>(std::time(nullptr));
  snapshot["file"] = file;

  json actions = json::array();
  if (history.is_array()) {
    for (const auto & entry : history) {
      const std::string name = StringMember(entry, "name");
      if (!IsReplayable(name)) continue;
      const json in = Member(Member(entry, "payload"), "in");
      if (in.is_null()) continue;
      json action;
      action["name"] = name;
      action["in"]   = in;
      // A request made through the gateway carries the room parameter in its query string,
      // which the server folds into the body as "_query". That is transport detail, not
      // part of the session, so it is kept out of the snapshot.
      if (action["in"].is_object()) action["in"].erase("_query");
      actions.push_back(std::move(action));
    }
  }

  // A file is open but nothing replayable was recorded (the history is in-memory and can be
  // reset): reopening it is still better than capturing nothing.
  if (actions.empty()) {
    json action;
    action["name"]       = kOpenRoute;
    action["in"]["file"] = file;
    actions.push_back(std::move(action));
  }
  snapshot["actions"] = actions;

  if (point.is_array() && !point.empty()) snapshot["point"] = point;
  return snapshot;
}

json NRoomSession::Capture(NHttpRequest & http, const std::string & roomBaseUrl, const std::string & roomId,
                           std::string & error, const std::string & token, State * reportedState)
{
  const std::string base = TrimBase(roomBaseUrl);

  std::string file;
  const State state = Probe(http, base, file, error, token);
  if (reportedState != nullptr) *reportedState = state;
  if (state == State::Unreachable) return json();
  // A room that refused the request said why in `error`. There is nothing to capture, and the
  // caller decides whether asking again is worth it (the router does not, until a new revision).
  if (state == State::Refused) return json();
  // Never let a room that has nothing open overwrite a good snapshot: a fresh pod is empty
  // for the first seconds of its life, which is exactly when a wake happens.
  if (state == State::Empty) return json();

  json root;
  if (!GetJson(http, base + "/api/", root, error, token)) return json();
  const json history = Member(Member(root, "state"), "history");

  // The state point is optional and lives outside the history (only a PATCH writes it), so
  // a room that cannot report it is still worth capturing.
  json point;
  if (!ReadPoint(http, base, point, error, token)) error.clear();

  return Build(roomId, file, history, point);
}

bool NRoomSession::RestoreInPlace(const json & snapshot, const Dispatch & dispatch, std::string & error)
{
  if (!snapshot.is_object()) {
    error = "the stored room snapshot is not a JSON object";
    return false;
  }

  const json actions = Member(snapshot, "actions");
  if (!actions.is_array() || actions.empty()) {
    error = "the stored room snapshot has no actions to replay";
    return false;
  }

  for (const auto & action : actions) {
    const std::string name = StringMember(action, "name");
    if (!IsReplayable(name)) {
      error = "the stored room snapshot contains an action that cannot be replayed: " + name;
      return false;
    }
    const json  in = Member(action, "in");
    std::string actionError;
    dispatch("POST", name, in.is_null() ? json::object() : in, actionError);
    if (!actionError.empty()) {
      error = actionError;
      return false;
    }
  }

  // No POST can set the state point, so it needs its own PATCH. That PATCH also tries to
  // render the projection, which fails in a room that has just started and has nothing
  // mapped yet - it answers "No entry and no projection found, nothing sent to websocket"
  // - even though the point itself is stored. So the effect is verified rather than the
  // reply trusted.
  const json point = Member(snapshot, "point");
  if (point.is_array() && !point.empty()) {
    json body;
    body["point"] = point;

    std::string patchError;
    dispatch("PATCH", kPointRoute, body, patchError);

    std::string readError;
    const json  stateDoc = dispatch("GET", "state", json::object(), readError);
    if (!readError.empty()) {
      error = "cannot confirm the restored state point: " + readError;
      return false;
    }

    const json spectra = Member(Member(Member(stateDoc, "payload"), "metadata"), "spectra");
    if (Member(spectra, "point") != point) {
      error = "the room did not keep the state point";
      if (!patchError.empty()) error += ": " + patchError;
      return false;
    }
  }

  return true;
}

bool NRoomSession::Restore(NHttpRequest & http, const std::string & roomBaseUrl, const json & snapshot,
                           std::string & error, const std::string & token)
{
  const std::string base = TrimBase(roomBaseUrl);

  // A room keeps HTTP 200 even when a handler fails, so a failed action is reported through
  // the dispatcher's error rather than by the transport. The response is still returned: a
  // caller that only wants the effect (the state point) reads the state back instead.
  const Dispatch dispatch = [&http, &base, &token](const std::string & method, const std::string & route,
                                                   const json & body, std::string & dispatchError) -> json {
    const std::string                  url = base + "/api/" + route;
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    if (!token.empty()) headers[NRoomAccess::kHeader] = token;

    NHttpResponse response;
    try {
      response = http.request(method, url, body.dump(), headers);
    }
    catch (const std::exception & e) {
      dispatchError = "cannot reach the room at " + url + " (" + e.what() + ")";
      return json();
    }
    if (response.status != 200) {
      dispatchError = method + " " + url + " answered HTTP " + std::to_string(response.status) + ": " +
                      Elide(response.body);
      return json();
    }

    json parsed;
    try {
      parsed = json::parse(response.body);
    }
    catch (const json::parse_error &) {
      dispatchError = method + " " + url + " returned a response that is not JSON: " + Elide(response.body);
      return json();
    }

    if (StringMember(parsed, "result") != "success") {
      const std::string detail = StringMember(parsed, "error");
      dispatchError = method + " " + url + " failed: " + (detail.empty() ? Elide(response.body) : detail);
    }
    return parsed;
  };

  return RestoreInPlace(snapshot, dispatch, error);
}

std::string NRoomSession::Encode(const json & snapshot)
{
  if (!snapshot.is_object() || snapshot.empty()) return {};

  const std::string text = snapshot.dump();
  if (text.size() > kMaxEncodedBytes) {
    NLogWarning("Room session snapshot is %zu bytes, over the %zu byte limit; not storing it", text.size(),
                kMaxEncodedBytes);
    return {};
  }
  return text;
}

bool NRoomSession::Decode(const std::string & text, json & snapshot)
{
  if (text.empty()) return false;

  try {
    json parsed = json::parse(text);
    if (!parsed.is_object()) return false;
    snapshot = std::move(parsed);
    return true;
  }
  catch (const json::parse_error &) {
    return false;
  }
}

} // namespace Ndmspc
