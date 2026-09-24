#include "NRoomClient.h"

#include <cstddef>
#include <string>
#include <utility>

#include "ndmspc/core/NUtils.h"
#include "ndmspc/ndmspc.h"

namespace Ndmspc {

namespace {

/// @brief The MCP protocol version this client speaks.
constexpr const char * kProtocolVersion = "2025-06-18";

/// @brief Shorten a string so a whole response body cannot flood an error message.
std::string Elide(const std::string & text, std::size_t maxLength = 200)
{
  if (text.size() <= maxLength) return text;
  return text.substr(0, maxLength) + "...";
}

/// @brief Read a member without throwing when it is absent.
/// @param object The JSON value to read from.
/// @param key The member name.
/// @return The member value, or a null JSON value when absent.
json Member(const json & object, const char * key)
{
  if (!object.is_object() || !object.contains(key)) return json();
  return object[key];
}

/// @brief Extract the message from a JSON-RPC or handler "error" value.
/// @param error The error value (string, object with "message", or anything else).
/// @return A printable message.
std::string ErrorText(const json & error)
{
  if (error.is_string()) return error.get<std::string>();
  if (error.is_object() && error.contains("message") && error["message"].is_string()) {
    return error["message"].get<std::string>();
  }
  return error.dump();
}

/// @brief True when a live room's pods are running.
/// @param object A room entry from room/list.
/// @return True when replicas is greater than zero.
bool ActiveOr(const json & object, bool fallback)
{
  if (!object.is_object() || !object.contains("active")) return fallback;
  return NUtils::GetJsonBool(object["active"]);
}

} // namespace

NRoomHttpClientImpl::NRoomHttpClientImpl(std::string certFile, std::string keyFile, std::string keyPasswordFile,
                                         std::string caFile, std::string caPath, bool insecure)
    : fCertFile(std::move(certFile)), fKeyFile(std::move(keyFile)), fKeyPasswordFile(std::move(keyPasswordFile)),
      fCaFile(std::move(caFile)), fCaPath(std::move(caPath)), fInsecure(insecure)
{
}

NHttpResponse NRoomHttpClientImpl::Post(const std::string & url, const std::string & body,
                                        const std::map<std::string, std::string> & headers)
{
  return fHttp.request("POST", url, body, headers, fCertFile, fKeyFile, fKeyPasswordFile, fCaFile, fCaPath,
                       fInsecure);
}

NRoomClient::NRoomClient(std::string endpoint, std::string bearerToken, std::shared_ptr<IRoomHttpClient> httpClient)
    : fEndpoint(std::move(endpoint)), fBearerToken(std::move(bearerToken)),
      fHttpClient(httpClient != nullptr ? std::move(httpClient) : std::make_shared<NRoomHttpClientImpl>())
{
}

std::string NRoomClient::McpEndpoint(const std::string & url)
{
  std::string endpoint = url;
  while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();

  const std::string suffix = "/api/mcp";
  if (endpoint.size() >= suffix.size() &&
      endpoint.compare(endpoint.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return endpoint;
  }
  return endpoint + suffix;
}

bool NRoomClient::Post(const json & message, std::string & error, std::string & body)
{
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  if (!fBearerToken.empty()) headers["Authorization"] = "Bearer " + fBearerToken;

  NHttpResponse response;
  try {
    response = fHttpClient->Post(fEndpoint, message.dump(), headers);
  }
  catch (const std::exception & e) {
    error = "cannot reach the room router at " + fEndpoint + " (" + e.what() + ")";
    return false;
  }

  if (response.status != 200) {
    error = "the room router at " + fEndpoint + " answered HTTP " + std::to_string(response.status);
    if (response.status == 401 || response.status == 403) {
      error += " - it requires authentication (see --oidc-*, or --cert/--key for mutual TLS)";
    }
    else if (response.status == 404) {
      error += " - no MCP endpoint is served at that URL";
    }
    if (!response.body.empty()) error += ": " + Elide(response.body);
    return false;
  }

  body = response.body;
  return true;
}

NRoomResult NRoomClient::ParseCallResponse(const std::string & body) const
{
  NRoomResult result;

  json envelope;
  try {
    envelope = json::parse(body);
  }
  catch (const json::parse_error &) {
    result.error = "the room router at " + fEndpoint + " returned a response that is not JSON: " + Elide(body);
    return result;
  }

  // A JSON-RPC level error: transport-level problems such as a disabled endpoint or
  // an unknown tool name arrive here rather than inside the tool result.
  if (envelope.contains("error") && !envelope["error"].is_null()) {
    result.error = ErrorText(envelope["error"]);
    if (result.error == "MCP endpoint is disabled") {
      result.error += " - start the server with --mcp true (NDMSPC_MCP=1)";
    }
    return result;
  }

  if (!envelope.contains("result") || !envelope["result"].is_object()) {
    result.error = "the room router at " + fEndpoint + " returned an unexpected JSON-RPC response: " + Elide(body);
    return result;
  }

  const json & mcpResult = envelope["result"];
  const bool   isError   = mcpResult.contains("isError") && NUtils::GetJsonBool(mcpResult["isError"]);

  // The handler's own JSON is echoed as structuredContent; fall back to the text
  // content, which carries the same JSON as a string.
  json handler;
  if (mcpResult.contains("structuredContent") && mcpResult["structuredContent"].is_object()) {
    handler = mcpResult["structuredContent"];
  }
  else {
    std::string text;
    if (mcpResult.contains("content") && mcpResult["content"].is_array() && !mcpResult["content"].empty()) {
      text = NUtils::GetJsonString(Member(mcpResult["content"][0], "text"));
    }
    if (text.empty()) {
      result.error = "the room router returned no room payload - is the room macro loaded? "
                     "(the router needs --rooms true / NDMSPC_ROOMS=1)";
      return result;
    }
    try {
      handler = json::parse(text);
    }
    catch (const json::parse_error &) {
      result.error = isError ? text : "the room router returned an unexpected payload: " + Elide(text);
      return result;
    }
  }

  // Handler-level failure: {"result":"failure","error":"..."}. These keep HTTP 200,
  // so this envelope - not the status code - is the real signal.
  if (NUtils::GetJsonString(Member(handler, "result")) == "failure") {
    result.error = NUtils::GetJsonString(Member(handler, "error"));
    if (result.error.empty()) result.error = "the room router reported a failure";
    return result;
  }

  if (isError) {
    result.error = handler.contains("error") ? ErrorText(handler["error"])
                                             : "the room router reported a failure: " + Elide(handler.dump());
    return result;
  }

  result.ok      = true;
  result.payload = handler.contains("payload") ? handler["payload"] : handler;
  return result;
}

NRoomResult NRoomClient::Call(const std::string & tool, const std::string & method, const std::string & roomId,
                              const json & extra)
{
  json arguments;
  arguments["method"] = method;
  if (!roomId.empty()) arguments["room"] = roomId;
  for (auto item = extra.begin(); item != extra.end(); ++item) {
    arguments[item.key()] = item.value();
  }
  // Who we are, when the caller of this client said: the router reads it as an asserted owner (an
  // explicit one in `extra` wins, and the server's own verified identity wins over both).
  if (!fOwner.empty() && !arguments.contains("owner")) arguments["owner"] = fOwner;

  json message;
  message["jsonrpc"]             = "2.0";
  message["id"]                  = ++fNextId;
  message["method"]              = "tools/call";
  message["params"]["name"]      = tool;
  message["params"]["arguments"] = arguments;

  NRoomResult result;
  std::string body;
  if (!Post(message, result.error, body)) return result;
  return ParseCallResponse(body);
}

bool NRoomClient::Initialize(std::string & error)
{
  json message;
  message["jsonrpc"]                          = "2.0";
  message["id"]                               = ++fNextId;
  message["method"]                           = "initialize";
  message["params"]["protocolVersion"]        = kProtocolVersion;
  message["params"]["clientInfo"]["name"]     = "ndmspc-room-tui";
  message["params"]["clientInfo"]["version"]  = std::string(NDMSPC_VERSION) + "-" + NDMSPC_VERSION_RELEASE;

  std::string body;
  if (!Post(message, error, body)) return false;

  json envelope;
  try {
    envelope = json::parse(body);
  }
  catch (const json::parse_error &) {
    error = "the room router at " + fEndpoint + " returned a response that is not JSON: " + Elide(body);
    return false;
  }

  if (envelope.contains("error") && !envelope["error"].is_null()) {
    error = ErrorText(envelope["error"]);
    if (error == "MCP endpoint is disabled") {
      error += " - start the server with --mcp true (NDMSPC_MCP=1)";
    }
    return false;
  }

  if (!envelope.contains("result") || !envelope["result"].is_object()) {
    error = "the room router at " + fEndpoint + " returned an unexpected JSON-RPC response: " + Elide(body);
    return false;
  }

  return true;
}

NRoomListResult NRoomClient::List()
{
  NRoomListResult list;

  const NRoomResult result = Call("room_list", "GET", "");
  if (!result.ok) {
    list.error = result.error;
    return list;
  }

  if (!result.payload.is_object()) {
    list.error = "the room router returned a room list that could not be read: " + Elide(result.payload.dump());
    return list;
  }

  const json & payload = result.payload;
  if (payload.contains("rooms") && payload["rooms"].is_array()) {
    for (const auto & entry : payload["rooms"]) {
      if (!entry.is_object()) continue;
      NRoomInfo room;
      room.name     = NUtils::GetJsonString(Member(entry, "name"));
      room.room     = NUtils::GetJsonString(Member(entry, "room"));
      room.revision = NUtils::GetJsonString(Member(entry, "revision"));
      room.lastSeen = NUtils::GetJsonInt(Member(entry, "lastSeen"));
      room.ready    = NUtils::GetJsonBool(Member(entry, "ready"));
      room.replicas = NUtils::GetJsonInt(Member(entry, "replicas"));
      // An idle room keeps its Service but runs no pods, so a missing "active" is
      // derived from replicas rather than assumed to be false.
      room.active = ActiveOr(entry, room.replicas > 0);
      // A room whose creation is still running is listed too, with what it is waiting for.
      room.state     = NUtils::GetJsonString(Member(entry, "state"));
      room.preparing = NUtils::GetJsonBool(Member(entry, "preparing"));
      room.phase     = NUtils::GetJsonString(Member(entry, "phase"));
      room.error     = NUtils::GetJsonString(Member(entry, "error"));
      room.code      = NUtils::GetJsonString(Member(entry, "code"));
      room.startedAt = NUtils::GetJsonInt(Member(entry, "startedAt"));
      // The size it runs at and what that allows: the rooms view shows both, so the TUI reads them
      // from the same payload rather than guessing at a room's shape.
      room.profile       = NUtils::GetJsonString(Member(entry, "profile"));
      const json resources = Member(entry, "resources");
      const json requests  = Member(resources, "requests");
      const json limits    = Member(resources, "limits");
      room.cpuRequest      = NUtils::GetJsonString(Member(requests, "cpu"));
      room.memoryRequest   = NUtils::GetJsonString(Member(requests, "memory"));
      room.cpuLimit        = NUtils::GetJsonString(Member(limits, "cpu"));
      room.memoryLimit     = NUtils::GetJsonString(Member(limits, "memory"));
      // Why it died last, while the router still knows: the one thing a room that was killed cannot
      // report about itself.
      const json lastError   = Member(entry, "lastError");
      room.lastErrorReason   = NUtils::GetJsonString(Member(lastError, "reason"));
      room.lastErrorMessage  = NUtils::GetJsonString(Member(lastError, "message"));
      room.lastErrorExit     = NUtils::GetJsonInt(Member(lastError, "exitCode"));
      room.lastErrorAt       = NUtils::GetJsonInt(Member(lastError, "at"));
      // The tokens that open the room. Empty for a room created before access existed, which is
      // also how a client tells that a room enforces nothing.
      const json access = Member(entry, "access");
      room.tokenRw      = NUtils::GetJsonString(Member(access, NRoomAccess::kReadWrite));
      room.tokenRo      = NUtils::GetJsonString(Member(access, NRoomAccess::kReadOnly));
      // Who the room belongs to: empty for a room created before ownership existed, or by a caller
      // that identified itself to nobody.
      room.owner = NUtils::GetJsonString(Member(entry, "owner"));
      list.rooms.push_back(std::move(room));
    }
  }
  if (payload.contains("ttl") && payload["ttl"].is_number()) list.ttl = payload["ttl"].get<int>();
  // Whether the router answered as an admin: the view says so rather than keeping its own copy of
  // the admin list, which could differ from the router's.
  list.admin = NUtils::GetJsonBool(Member(payload, "admin"));

  list.ok = true;
  return list;
}

std::string NRoomInfo::TokenFor(const std::string & level) const
{
  return level == NRoomAccess::kReadOnly ? tokenRo : tokenRw;
}

NRoomResult NRoomClient::Open(const std::string & roomId, bool wait)
{
  // Always send the flag: which way this call behaves should not depend on the router's default.
  json extra;
  extra["wait"] = wait;
  return Call("room_open", "POST", roomId, extra);
}

NRoomResult NRoomClient::Status(const std::string & roomId) { return Call("room_status", "GET", roomId); }

NRoomResult NRoomClient::Close(const std::string & roomId) { return Call("room_close", "DELETE", roomId); }

NRoomResult NRoomClient::Backup() { return Call("room_backup", "GET", ""); }

NRoomResult NRoomClient::Restore(const json & document, bool replace)
{
  json extra;
  extra["document"] = document;
  // Sent only when asked for: a restore is additive otherwise, and a room that is already in use keeps
  // its own session. `replace` is what makes the document's session win (the room is deleted first).
  if (replace) extra["replace"] = true;
  return Call("room_restore", "POST", "", extra);
}

} // namespace Ndmspc
