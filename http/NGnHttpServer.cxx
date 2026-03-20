#include <TROOT.h>
#include "NGnHttpServer.h"
#include "NGnHistoryEntry.h"
#include "NLogger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnHttpServer);
/// \endcond

namespace Ndmspc {
NGnHttpHandlerMap * gNdmspcHttpHandlers = nullptr;
NGnHttpServer *     gNGnHttpServer      = nullptr;
NGnHttpServer::NGnHttpServer(const char * engine, bool ws, int heartbeat_ms) : NHttpServer(engine, ws, heartbeat_ms)
{
  Ndmspc::gNGnHttpServer = this;
  fWorkspace.SetServer(this);
}

void NGnHttpServer::Print(Option_t * option) const
{
  NHttpServer::Print(option);
  // print HTTP handlers
  // NLogInfo("HTTP Handlers:");
  // for (const auto & handler : fHttpHandlers) {
  //   NLogInfo("  %s", handler.first.c_str());
  // }
  // print all input objects
  NLogInfo("Input Objects:");
  for (const auto & obj : fObjectsMap) {
    NLogInfo("  %s -> %p", obj.first.c_str(), obj.second);
  }
  // print history entries
  fWorkspace.Print(option);
}

void NGnHttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{

  // NLogInfo("NGnHttpServer::ProcessRequest");

  TString method   = arg->GetMethod();
  TString path     = arg->GetPathName();
  TString filename = arg->GetFileName();
  // if (arg->GetRequestHeader("Content-Type").CompareTo("application/json")) {
  //   // NLogWarning("Unsupported Content-Type: %s", arg->GetRequestHeader("Content-Type").Data());
  //   NHttpServer::ProcessRequest(arg);
  //   return;
  // }

  NLogTrace("Received %s request for path: %s filename: %s", method.Data(), path.Data(), filename.Data());

  TString fullpath = TString::Format("/%s/%s/", path.Data(), filename.Data()).Data();
  fullpath.ReplaceAll("//", "/");
  // Yes it needs to be done twice to handle cases where both path and filename are empty resulting in "///"
  fullpath.ReplaceAll("//", "/");

  NLogTrace("Constructed full path: %s", fullpath.Data());
  // if fullpath does not start with "/api" or "api", process it with base class handler

  if (!(fullpath.BeginsWith("/api/"))) {
    NLogTrace("Using base http server for path: %s", fullpath.Data());
    NHttpServer::ProcessRequest(arg);
    return;
  }

  fullpath.Remove(0, 4);
  fullpath          = fullpath.Strip(TString::kLeading, '/');
  fullpath          = fullpath.Strip(TString::kTrailing, '/');
  std::string query = arg->GetQuery();
  NLogTrace("Processing %s request for path: %s query: %s", method.Data(), fullpath.Data(), query.c_str());

  json out;
  json wsOut;
  if (fullpath.IsNull()) {

    out["result"]  = "success";
    out["message"] = "Welcome to NGnHttpServer API";
    // out["ws"]["path"] = "ws/root.websocket";

    out["state"]["history"]   = GetJson();
    out["state"]["users"]     = fNWsHandler ? fNWsHandler->GetClientCount() : 0;
    out["state"]["workspace"] = GetWorkspace();

    // Derive group from handler keys if not yet set by a handler call
    if (fGroup.empty()) {
      for (const auto & h : fHttpHandlers) {
        auto pos = h.first.find('/');
        if (pos != std::string::npos) {
          fGroup = h.first.substr(0, pos);
          break;
        }
      }
    }
    if (!fGroup.empty()) {
      out["state"]["group"] = fGroup;
    }
  }
  else {

    json in;
    try {
      std::string content = (const char *)arg->GetPostData();
      if (!content.empty()) in = json::parse(content);
    }
    catch (json::parse_error & e) {
      NLogError("JSON parse error: %s", e.what());
      arg->SetContentType("application/json");
      arg->SetContent("{\"error\": \"Invalid JSON format\"}");
      return;
    }
    NLogTrace("Received %s request with content: %s", method.Data(), in.dump().c_str());

    // Special-case: provide an OpenAPI-compatible inspector schema endpoint
    if (fullpath == "openapi/inspector" || fullpath == "inspector/openapi") {
      json openapi;
      openapi["openapi"] = "3.0.0";
      openapi["info"]["title"] = std::string("NGn Inspector for ") + GetName();
      openapi["info"]["version"] = "1.0.0";
      // Put the inspector schema under components.schemas.NGn_Workspace
      json inspectorSchema = GetInspectorSchema();
      // If inspector contains properties, use that as the schema, otherwise include whole inspector
      if (!inspectorSchema["inspector"]["properties"].is_null()) {
        openapi["components"]["schemas"]["NGn_Workspace"] = inspectorSchema["inspector"]["properties"];
      }
      else {
        openapi["components"]["schemas"]["NGn_Workspace"] = inspectorSchema;
      }
      out = openapi;
    }
    else if (fHttpHandlers.find(fullpath.Data()) == fHttpHandlers.end()) {
      NLogError("Unsupported action: %s", fullpath.Data());
      arg->SetContentType("application/json");
      arg->SetContent("{\"error\": \"Unsupported action\"}");
      return;
    }

    // fObjectsMap["_httpServer"] = this;

    NGnHistoryEntry * historyEntry = nullptr;
    if (fUseHistory) {
      if (!method.CompareTo("POST")) {
        NLogTrace("Adding history entry for path: %s", fullpath.Data());
        historyEntry = new NGnHistoryEntry(fullpath.Data(), method.Data());
        historyEntry->SetPayloadIn(in);
        NLogTrace("History entry created for path: %s with payload: %s", fullpath.Data(), in.dump().c_str());
        fWorkspace.AddEntry(historyEntry);
      }
    }

    fHttpHandlers[fullpath.Data()](method.Data(), in, out, wsOut, fObjectsMap);

    if (fUseHistory) {
      NLogTrace("HTTP handler output for path %s: %s", fullpath.Data(), out.dump().c_str());
      if (!out["result"].is_null() && !out["result"].get<std::string>().compare("success")) {
        if (!method.CompareTo("POST")) {
          if (historyEntry) {
            historyEntry->SetPayloadOut(out);
            historyEntry->SetPayloadWsOut(wsOut);
          }
        }
        else if (!method.CompareTo("DELETE")) {
          fWorkspace.RemoveEntry(fullpath.Data());
        }
      }
      else {
        if (!method.CompareTo("POST")) {
          fWorkspace.RemoveEntry(static_cast<int>(fWorkspace.GetEntries().size()) - 1);
        }
      }
    }

    // Don't broadcast workspace updates in response to DELETE requests
    if (!method.CompareTo("DELETE")) {
      wsOut["workspace"] = nullptr;
    }

    if (!wsOut["payload"].is_null() || !wsOut["workspace"].is_null() || !wsOut["state"].is_null()) {
      json wsMessage;
      wsMessage["event"]   = "ngnt";
      wsMessage["payload"] = wsOut["payload"].is_null() ? json::object() : wsOut["payload"];
      // If state is present, include it in the same payload
      if (!wsOut["state"].is_null()) {
        wsMessage["payload"]["state"] = wsOut["state"];
      }

      // loop over keys in wsOut["workspace"] and add them to workspace, overwriting existing ones if necessary
      if (!wsOut["workspace"].is_null()) {
        // Build workspace with order of keys same as in NGnHistoryEntry
        json workspace;
        for (const auto & entry : fWorkspace.GetEntries()) {
          // History entries use full path (e.g. "ngnt/open"), but workspace uses short keys ("open")
          std::string entryName = entry->GetName();
          std::string wsKey     = entryName;
          if (!fGroup.empty() && entryName.rfind(fGroup + "/", 0) == 0) {
            wsKey = entryName.substr(fGroup.size() + 1);
          }
          NLogTrace("Adding workspace entry for: %s (wsKey: %s)", entryName.c_str(), wsKey.c_str());
          if (!GetWorkspace()[wsKey].is_null()) {
            workspace[wsKey] = GetWorkspace()[wsKey];
          }
        }

        for (auto it = wsOut["workspace"].begin(); it != wsOut["workspace"].end(); ++it) {
          NLogTrace("Updating workspace entry for: %s", it.key().c_str());
          // if value is null, skip it
          if (it.value().is_null()) {
            NLogTrace("Skipping null workspace entry for: %s", it.key().c_str());
            continue;
          }

          workspace[it.key()]         = it.value();
          workspace[it.key()]["type"] = "object";
          GetWorkspace()[it.key()]    = it.value();
        }
        wsMessage["payload"]["workspace"]["schema"]["properties"] = workspace;

        // Pass through group prefix if set by handler macro
        if (wsOut.contains("group") && wsOut["group"].is_string()) {
          wsMessage["payload"]["workspace"]["schema"]["group"] = wsOut["group"];
          if (fGroup.empty()) {
            fGroup = wsOut["group"].get<std::string>();
          }
        }
      }

      NLogDebug("Broadcasting to WebSocket clients for path %s: %s", fullpath.Data(), wsMessage.dump().c_str());
      WebSocketBroadcast(wsMessage);
    }
    else {
      NLogTrace("Skipping WebSocket broadcast for path %s: no payload or workspace changes", fullpath.Data());
    }
  }

  // arg->AddHeader("X-Header", "Test");
  arg->AddHeader("Access-Control-Allow-Origin", GetCors());
  arg->SetContentType("application/json");
  arg->SetContent(out.dump());
  // arg->SetContent("ok");
  // arg->SetContentType("text/plain");
}

TObject * NGnHttpServer::GetInputObject(const std::string & name)
{
  if (fObjectsMap.find(name) != fObjectsMap.end()) {
    return fObjectsMap[name];
  }
  return nullptr;
}

void NGnHttpServer::ResetServer()
{
  ///
  /// Clear workspace history first so DELETE handlers can still access input
  /// objects, then remove any remaining objects that weren't cleaned up by
  /// the handlers.
  ///
  NLogInfo("NGnHttpServer::ResetServer: Clearing history ...");
  ClearHistory();
  NLogInfo("NGnHttpServer::ResetServer: Removing remaining input objects ...");
  std::vector<std::string> keys;
  keys.reserve(fObjectsMap.size());
  for (const auto & pair : fObjectsMap) {
    keys.push_back(pair.first);
  }
  for (const auto & key : keys) {
    NLogInfo("NGnHttpServer::ResetServer: Removing input object '%s'", key.c_str());
    RemoveInputObject(key);
  }
  NLogInfo("NGnHttpServer::ResetServer: Done.");
}

bool NGnHttpServer::RemoveInputObject(const std::string & name)
{
  if (fObjectsMap.find(name) != fObjectsMap.end()) {
    TObject * obj = fObjectsMap[name];
    if (obj) {
      NLogDebug("Removing input object: %s", name.c_str());
      delete obj;
    }
    fObjectsMap.erase(name);
  }

  bool stillExists = (fObjectsMap.find(name) != fObjectsMap.end());
  return !stillExists;
}
// For compatibility, provide GetJson and Export/Load wrappers
json NGnHttpServer::GetJson() const
{
  json historyJson = json::array();

  for (const auto & entry : fWorkspace.GetEntries()) {
    json entryJson;
    entryJson["name"]             = entry->GetName();
    entryJson["method"]           = "POST";
    entryJson["payload"]["in"]    = entry->GetPayloadIn();
    entryJson["payload"]["out"]   = entry->GetPayloadOut();
    entryJson["payload"]["wsOut"] = entry->GetPayloadWsOut();
    historyJson.push_back(entryJson);
  }
  return historyJson;
}

} // namespace Ndmspc
