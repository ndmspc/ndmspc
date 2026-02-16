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
  fHistory.SetServer(this);
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
  fHistory.Print(option);
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

    out["state"]["history"] = GetJson();
    out["state"]["users"]   = fNWsHandler ? fNWsHandler->GetClientCount() : 0;
    out["state"]["workspace"] = GetWorkspace();
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

    if (fHttpHandlers.find(fullpath.Data()) == fHttpHandlers.end()) {
      NLogError("Unsupported action: %s", fullpath.Data());
      arg->SetContentType("application/json");
      arg->SetContent("{\"error\": \"Unsupported action\"}");
      return;
    }

    // fObjectsMap["_httpServer"] = this;

    NGnHistoryEntry * historyEntry = nullptr;
    if (!method.CompareTo("POST")) {
      NLogTrace("Adding history entry for path: %s", fullpath.Data());
      historyEntry = new NGnHistoryEntry(fullpath.Data(), method.Data());
      historyEntry->SetPayloadIn(in);
      NLogDebug("History entry created for path: %s with payload: %s", fullpath.Data(), in.dump().c_str());
      fHistory.AddEntry(historyEntry);
    }

    fHttpHandlers[fullpath.Data()](method.Data(), in, out, wsOut, fObjectsMap);
    NLogTrace("HTTP handler output for path %s: %s", fullpath.Data(), out.dump().c_str());
    if (!out["result"].is_null() && !out["result"].get<std::string>().compare("success")) {
      if (!method.CompareTo("POST")) {
        if (historyEntry) {
          historyEntry->SetPayloadOut(out);
          historyEntry->SetPayloadWsOut(wsOut);
        }
      }
      else if (!method.CompareTo("DELETE")) {
        fHistory.RemoveEntry(fullpath.Data());
      }
    }
    else {
      if (!method.CompareTo("POST")) {
        fHistory.RemoveEntry(static_cast<int>(fHistory.GetEntries().size()) - 1);
      }
    }
  }
  // if (!wsOut.empty()) {

  json wsMessage;
  wsMessage["event"]   = "ngnt";
  wsMessage["payload"] = wsOut["payload"].is_null() ? json::object() : wsOut["payload"];
  // wsMessage["payload"]["workspace"]["schema"]["properties"] = GetWorkspace();

  // loop over keys in wsOut["workspace"] and add them to workspace, overwriting existing ones if necessary
  if (!wsOut["workspace"].is_null()) {
    // Build workspace with order of keys same as in NGnHistoryEntry
    json workspace;
    for (const auto & entry : fHistory.GetEntries()) {
      NLogDebug("Adding workspace entry for: %s", entry->GetName());
      workspace[entry->GetName()] = GetWorkspace()[entry->GetName()];
    }

    for (auto it = wsOut["workspace"].begin(); it != wsOut["workspace"].end(); ++it) {
      NLogDebug("Updating workspace entry for: %s", it.key().c_str());
      // if value us null, skip it
      if (it.value().is_null()) {
        NLogDebug("Skipping null workspace entry for: %s", it.key().c_str());
        continue;
      }

      workspace[it.key()]         = it.value();
      workspace[it.key()]["type"] = "object";
      GetWorkspace()[it.key()]    = it.value();
    }
    wsMessage["payload"]["workspace"]["schema"]["properties"] = workspace;
  }

  NLogDebug("Broadcasting to WebSocket clients for path %s: %s", fullpath.Data(), wsMessage.dump().c_str());
  WebSocketBroadcast(wsMessage);
  // }
  // Print();
  // out["status"] = "ok";

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

  for (const auto & entry : fHistory.GetEntries()) {
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
