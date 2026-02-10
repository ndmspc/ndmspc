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
  NLogInfo("History Entries:");
  for (size_t i = 0; i < fHistory.size(); i++) {
    NLogInfo("  [%zu]: %s payload: in=%s out=%s wsOut=%s", i, fHistory[i]->GetName(),
             fHistory[i]->GetPayloadIn().dump().c_str(), fHistory[i]->GetPayloadOut().dump().c_str(),
             fHistory[i]->GetPayloadWsOut().dump().c_str());
  }
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
      AddHistoryEntry(historyEntry);
    }

    fHttpHandlers[fullpath.Data()](method.Data(), in, out, wsOut, fObjectsMap);
    NLogTrace("HTTP handler output for path %s: %s", fullpath.Data(), out.dump().c_str());
    if (!out["result"].is_null() && !out["result"].get<std::string>().compare("success")) {
      if (!method.CompareTo("POST")) {
        historyEntry->SetPayloadOut(out);
        historyEntry->SetPayloadWsOut(wsOut);
        // AddHistoryEntry(historyEntry);
      }
      else if (!method.CompareTo("DELETE")) {
        RemoveHistoryEntry(fullpath.Data());
      }
      // Print();
      // ExportHistoryToFile("/tmp/ngnt_http_history.json");
    }
    else {
      if (!method.CompareTo("POST")) {
        RemoveHistoryEntry(fHistory.size() - 1);
      }
    }
  }
  if (!wsOut.empty()) {
    NLogDebug("Broadcasting to WebSocket clients for path %s: %s", fullpath.Data(), wsOut.dump().c_str());
    json wsMessage;
    wsMessage["event"]   = "ngnt";
    wsMessage["payload"] = wsOut;
    WebSocketBroadcast(wsMessage);
  }
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

void NGnHttpServer::AddHistoryEntry(NGnHistoryEntry * entry)
{
  RemoveHistoryEntry(entry->GetName());

  NLogTrace("Adding history entry: %s", entry->GetName());
  fHistory.push_back(entry);
}

bool NGnHttpServer::RemoveHistoryEntry(const std::string & name)
{
  // Find if entry exits and remove it along with all newer entries
  bool found = false;
  for (int i = static_cast<int>(fHistory.size()) - 1; i >= 0; i--) {
    NLogTrace("Checking history entry at index %d: %s", i, fHistory[i]->GetName());
    std::string existingName = fHistory[i]->GetName();
    if (!existingName.compare(name)) {
      NLogTrace("Found existing history entry with same name: %s at index %d, removing newer entries.", name.c_str(),
                i);
      // remove all entries above it
      int j = -1;
      for (j = fHistory.size() - 1; j > static_cast<int>(i); j--) {
        NLogTrace("Removing history entry at index %zu: %s", j, fHistory[j]->GetName());

        RemoveHistoryEntry(j);
      }
      if (j >= 0) RemoveHistoryEntry(j);
      found = true;
      break;
    }
  }

  return found;
}

bool NGnHttpServer::RemoveHistoryEntry(int index)
{
  if (index < 0 || index >= static_cast<int>(fHistory.size())) {
    NLogError("Invalid history entry index: %d", index);
    return false;
  }

  NGnHistoryEntry * entry = fHistory[index];
  json              in    = entry->GetPayloadIn();
  json              out;
  json              wsOut;
  NLogTrace("Removing history entry: %s", entry->GetName());
  NLogTrace("Config: %s", in.dump().c_str());
  NLogTrace("Invoking HTTP handler for DELETE on entry: %s", entry->GetName());
  fHttpHandlers[entry->GetName()]("DELETE", in, out, wsOut, fObjectsMap);
  delete entry;

  fHistory.erase(fHistory.begin() + index);
  return true;
}

void NGnHttpServer::ClearHistory()
{
  // Remove all input objects
  NLogTrace("Clearing all input objects.");
  for (const auto & obj : fObjectsMap) {
    NLogTrace("Removing input object: %s", obj.first.c_str());
    delete obj.second;
  }
  fObjectsMap.clear();
  NLogTrace("Clearing %zu history entries.", fHistory.size());
  for (auto entry : fHistory) {
    delete entry;
  }
  fHistory.clear();
  Print();
}

bool NGnHttpServer::LoadHistoryFromFile(const std::string & filename)
{
  NLogInfo("Loading history from file: %s", filename.c_str());
  json cfg;
  return NUtils::LoadJsonFile(cfg, filename);
}

json NGnHttpServer::GetJson() const
{
  json historyJson = json::array();
  for (const auto & entry : fHistory) {
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

bool NGnHttpServer::ExportHistoryToFile(const std::string & filename) const
{
  NLogInfo("Exporting history to file: %s", filename.c_str());

  json historyJson = GetJson();
  bool rc          = NUtils::SaveRawFile(filename, historyJson.dump());
  if (rc)
    NLogInfo("Successfully exported history to file: %s", filename.c_str());
  else
    NLogError("Failed to export history to file: %s", filename.c_str());
  return rc;
}

} // namespace Ndmspc
