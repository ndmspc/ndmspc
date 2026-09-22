#include "NMcpServer.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include <THttpCallArg.h>

#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/ndmspc.h"

namespace Ndmspc {
namespace {

/// @brief Return the part of a handler key after the group prefix ("ngnt/open" -> "open").
std::string ShortKey(const std::string & handlerKey)
{
  const auto pos = handlerKey.find('/');
  return pos == std::string::npos ? handlerKey : handlerKey.substr(pos + 1);
}

json ResultResponse(const json & id, const json & result)
{
  json response;
  response["jsonrpc"] = "2.0";
  response["id"]      = id;
  response["result"]  = result;
  return response;
}

json ErrorResponse(const json & id, int code, const std::string & message)
{
  json response;
  response["jsonrpc"]     = "2.0";
  response["id"]          = id;
  response["error"]["code"]    = code;
  response["error"]["message"] = message;
  return response;
}

} // namespace

NMcpServer::NMcpServer(NHttpServer * server) : NMcpServer(server, Options{}) {}

NMcpServer::NMcpServer(NHttpServer * server, Options opts) : fServer(server), fOpts(std::move(opts))
{
  if (fOpts.serverVersion.empty()) {
    fOpts.serverVersion = std::string(NDMSPC_VERSION) + "-" + NDMSPC_VERSION_RELEASE;
  }
}

std::string NMcpServer::ToolName(const std::string & handlerKey)
{
  std::string name;
  name.reserve(handlerKey.size());
  bool previousUnderscore = false;
  for (char c : handlerKey) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool          plain = (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9');
    if (plain || c == '_' || c == '-') {
      name.push_back(c);
      previousUnderscore = false;
    }
    else if (!previousUnderscore) {
      name.push_back('_');
      previousUnderscore = true;
    }
  }
  if (name.size() > 64) name.resize(64);
  return name;
}

const NMcpToolInfo * NMcpServer::LookupToolInfo(const std::string & handlerKey) const
{
  if (gNdmspcMcpTools == nullptr) return nullptr;
  const auto it = gNdmspcMcpTools->find(handlerKey);
  return it != gNdmspcMcpTools->end() ? &it->second : nullptr;
}

bool NMcpServer::IsExcluded(const std::string & handlerKey) const
{
  const NMcpToolInfo * info = LookupToolInfo(handlerKey);
  if (info != nullptr && info->hidden) return true;
  if (std::find(fOpts.excludeActions.begin(), fOpts.excludeActions.end(), handlerKey) != fOpts.excludeActions.end()) {
    return true;
  }
  if (!fOpts.toolPrefixes.empty()) {
    for (const auto & prefix : fOpts.toolPrefixes) {
      if (handlerKey == prefix || handlerKey.rfind(prefix + "/", 0) == 0) return false;
    }
    return true;
  }
  return false;
}

std::string NMcpServer::Describe(const std::string & handlerKey) const
{
  const NMcpToolInfo * info = LookupToolInfo(handlerKey);
  if (info != nullptr && !info->description.empty()) return info->description;
  return "NGnTree action '" + handlerKey + "'.";
}

std::string NMcpServer::FindHandlerKey(const std::string & toolName) const
{
  if (!fServer) return {};
  const auto handlers = fServer->GetHttpHandlers();
  for (const auto & [key, fn] : handlers) {
    (void)fn;
    if (IsExcluded(key)) continue;
    if (ToolName(key) == toolName) return key;
  }
  return {};
}

json NMcpServer::BuildTools() const
{
  json tools = json::array();
  if (!fServer) return json{{"tools", tools}};

  json inspectorProperties = json::object();
  try {
    const json schema = fServer->GetInspectorSchema();
    if (schema.contains("inspector") && schema["inspector"].is_object() &&
        schema["inspector"].contains("properties") && schema["inspector"]["properties"].is_object()) {
      inspectorProperties = schema["inspector"]["properties"];
    }
  }
  catch (const std::exception &) {
  }

  std::set<std::string> usedNames;
  const auto            handlers = fServer->GetHttpHandlers();
  for (const auto & [key, fn] : handlers) {
    (void)fn;
    if (IsExcluded(key)) continue;

    const std::string toolName = ToolName(key);
    if (toolName.empty()) continue;
    if (!usedNames.insert(toolName).second) continue; // skip name collisions

    const std::string shortKey = ShortKey(key);

    const NMcpToolInfo * info = LookupToolInfo(key);

    json inputSchema = json::object();
    if (inspectorProperties.contains(shortKey) && inspectorProperties[shortKey].is_object()) {
      inputSchema = inspectorProperties[shortKey];
    }
    if (!inputSchema.contains("type") || inputSchema["type"].is_null()) inputSchema["type"] = "object";
    if (!inputSchema.contains("properties") || !inputSchema["properties"].is_object()) {
      inputSchema["properties"] = json::object();
    }

    // Merge any extra schema declared by the macro (properties are merged per key).
    if (info != nullptr && info->inputSchema.is_object()) {
      for (auto it = info->inputSchema.begin(); it != info->inputSchema.end(); ++it) {
        if (it.key() == "properties" && it.value().is_object()) {
          for (auto prop = it.value().begin(); prop != it.value().end(); ++prop) {
            inputSchema["properties"][prop.key()] = prop.value();
          }
        }
        else {
          inputSchema[it.key()] = it.value();
        }
      }
    }

    // Accept arguments that are not declared here. The inspector schema is populated
    // lazily (built from the workspace on first use), so before an action has run its
    // properties are unknown; without this, clients that validate arguments against the
    // schema drop every undeclared parameter (e.g. 'file' for ngnt/open). A macro may
    // still set additionalProperties explicitly to tighten its tool.
    if (!inputSchema.contains("additionalProperties")) inputSchema["additionalProperties"] = true;

    if (fOpts.exposeMethodParam) {
      json method;
      method["type"]        = "string";
      method["description"] = "HTTP verb for this action (default: POST).";
      if (info != nullptr && !info->methods.empty()) {
        method["enum"]     = info->methods;
        const bool hasPost = std::find(info->methods.begin(), info->methods.end(), "POST") != info->methods.end();
        method["default"]  = hasPost ? "POST" : info->methods.front();
      }
      else {
        method["enum"]    = {"GET", "POST", "PATCH", "DELETE"};
        method["default"] = "POST";
      }
      inputSchema["properties"]["method"] = method;
    }

    json tool;
    tool["name"] = toolName;
    if (info != nullptr && !info->title.empty()) tool["title"] = info->title;
    tool["description"] = Describe(key);
    tool["inputSchema"] = inputSchema;
    tools.push_back(tool);
  }

  return json{{"tools", tools}};
}

json NMcpServer::CallTool(const std::string & toolName, const json & arguments) const
{
  json content = json::array();

  const std::string handlerKey = FindHandlerKey(toolName);
  if (!fServer || handlerKey.empty()) {
    content.push_back({{"type", "text"}, {"text", "Unknown tool: " + toolName}});
    return json{{"content", content}, {"isError", true}};
  }

  json in = arguments.is_object() ? arguments : json::object();
  std::string method = "POST";
  if (in.contains("method") && in["method"].is_string()) {
    method = in["method"].get<std::string>();
    in.erase("method");
  }

  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod(method.c_str());
  arg->SetPathName("api");
  arg->SetFileName(handlerKey.c_str());
  if (!in.empty()) arg->SetPostData(in.dump());

  // Run as the caller that reached this endpoint: the synthetic request below is built from the
  // tool's arguments alone, so nothing else would say who asked (see SetCallerIdentity).
  fServer->ProcessRequestAs(arg, fCallerIdentity);

  std::string text;
  if (arg->GetContent() != nullptr && arg->GetContentLength() > 0) {
    text.assign(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
  }

  bool isError    = false;
  json structured = nullptr;
  if (!text.empty()) {
    try {
      structured = json::parse(text);
      if (structured.is_object() && structured.contains("error") && !structured["error"].is_null()) isError = true;
    }
    catch (const json::parse_error &) {
      structured = nullptr;
    }
  }

  content.push_back({{"type", "text"}, {"text", text}});
  json result;
  result["content"] = content;
  if (!structured.is_null()) result["structuredContent"] = structured;
  result["isError"] = isError;
  return result;
}

json NMcpServer::Handle(const json & message) const
{
  if (!message.is_object()) return ErrorResponse(nullptr, -32600, "Invalid Request");

  const bool hasId = message.contains("id") && !message["id"].is_null();
  const json id    = hasId ? message["id"] : json(nullptr);

  if (!message.contains("method") || !message["method"].is_string()) {
    return hasId ? ErrorResponse(id, -32600, "Invalid Request") : json(nullptr);
  }

  const std::string method = message["method"].get<std::string>();
  const json        params = message.contains("params") && message["params"].is_object() ? message["params"] : json::object();

  // Notifications carry no id and never produce a response.
  if (!hasId) return json(nullptr);

  if (method == "initialize") {
    json result;
    result["protocolVersion"] = fOpts.protocolVersion;
    if (params.contains("protocolVersion") && params["protocolVersion"].is_string()) {
      result["protocolVersion"] = params["protocolVersion"].get<std::string>();
    }
    result["capabilities"]["tools"]["listChanged"] = false;
    result["serverInfo"]["name"]                   = fOpts.serverName;
    result["serverInfo"]["version"]                = fOpts.serverVersion;
    return ResultResponse(id, result);
  }

  if (method == "ping") return ResultResponse(id, json::object());

  if (method == "tools/list") return ResultResponse(id, BuildTools());

  if (method == "tools/call") {
    if (!params.contains("name") || !params["name"].is_string()) {
      return ErrorResponse(id, -32602, "Invalid params: 'name' is required");
    }
    const std::string toolName = params["name"].get<std::string>();
    if (FindHandlerKey(toolName).empty()) {
      return ErrorResponse(id, -32602, "Unknown tool: " + toolName);
    }
    const json arguments = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"]
                                                                                           : json::object();
    return ResultResponse(id, CallTool(toolName, arguments));
  }

  return ErrorResponse(id, -32601, "Method not found: " + method);
}

json NMcpServer::HandleText(const std::string & text) const
{
  json message;
  try {
    message = json::parse(text);
  }
  catch (const json::parse_error &) {
    return ErrorResponse(nullptr, -32700, "Parse error");
  }
  return Handle(message);
}

} // namespace Ndmspc
