#include <NGnTree.h>
#include <TTimer.h>
#include "NHttpServer.h"
#include "NLogger.h"
#include "NGnHttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnHttpServer);
/// \endcond

namespace Ndmspc {
NGnHttpHandlerMap * gNdmspcHttpHandlers = nullptr;
NGnHttpServer::NGnHttpServer(const char * engine, bool ws, int heartbeat_ms) : NHttpServer(engine, ws, heartbeat_ms) {}

void NGnHttpServer::Print(Option_t * option) const
{
  NHttpServer::Print(option);
  // print HTTP handlers
  NLogInfo("HTTP Handlers:");
  for (const auto & handler : fHttpHandlers) {
    NLogInfo("  %s", handler.first.c_str());
  }
  // print all input objects
  NLogInfo("Input Objects:");
  for (const auto & obj : fObjectsMap) {
    NLogInfo("  %s -> %p", obj.first.c_str(), obj.second);
  }

  // print history entries
  NLogInfo("History Entries:");
  for (size_t i = 0; i < fHistory.size(); i++) {
    NLogInfo("  [%zu]: %s cfg: %s", i, fHistory[i]->GetName(), fHistory[i]->GetConfig().dump().c_str());
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

  NLogDebug("Received %s request for path: %s filename: %s", method.Data(), path.Data(), filename.Data());

  // if path is not /api/*
  if (!path.BeginsWith("api")) {
    NHttpServer::ProcessRequest(arg);
    return;
  }

  TString fullpath = TString::Format("%s/%s", path.Data(), filename.Data()).Data();
  fullpath.ReplaceAll("api/", "");
  fullpath.ReplaceAll("//", "/");
  fullpath          = fullpath.Strip(TString::kTrailing, '/');
  std::string query = arg->GetQuery();
  NLogDebug("Processing %s request for path: %s query: %s", method.Data(), fullpath.Data(), query.c_str());

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
  NLogInfo("Received %s request with content: %s", method.Data(), in.dump().c_str());

  if (fHttpHandlers.find(fullpath.Data()) == fHttpHandlers.end()) {
    NLogError("Unsupported action: %s", fullpath.Data());
    arg->SetContentType("application/json");
    arg->SetContent("{\"error\": \"Unsupported action\"}");
    return;
  }

  fObjectsMap["_httpServer"] = this;

  json out;
  fHttpHandlers[fullpath.Data()](method.Data(), in, out, fObjectsMap);
  NGnHistoryEntry * historyEntry = new NGnHistoryEntry(fullpath.Data(), method.Data());
  historyEntry->SetConfig(in);
  AddHistoryEntry(historyEntry);
  // Print();
  // ExportHistoryToFile("/tmp/ngnt_http_history.json");

  // out["status"] = "ok";

  // arg->AddHeader("X-Header", "Test");
  arg->AddHeader("Access-Control-Allow-Origin", "*");
  arg->SetContentType("application/json");
  arg->SetContent(out.dump());
  // arg->SetContent("ok");
  // arg->SetContentType("text/plain");
}

bool NGnHttpServer::WebSocketBroadcast(json message)
{
  NLogDebug("NGnHttpServer::WebSocketBroadcast: Broadcasting message to all clients.");
  if (fNWsHandler) {
    std::string msgStr = message.dump();
    fNWsHandler->BroadcastUnsafe(msgStr);
    return true;
  }
  return false;
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

  // remove top entries if entry with same name exists
  // loop from most recent to oldest
  // if found remove all entries above it
  for (int i = static_cast<int>(fHistory.size()) - 1; i >= 0; i--) {
    NLogDebug("Checking history entry at index %d: %s", i, fHistory[i]->GetName());
    std::string existingName = fHistory[i]->GetName();
    if (!existingName.compare(entry->GetName())) {
      NLogDebug("Found existing history entry with same name: %s at index %d, removing newer entries.",
                entry->GetName(), i);
      // remove all entries above it
      int j = -1;
      for (j = fHistory.size() - 1; j > static_cast<int>(i); j--) {
        NLogDebug("Removing history entry at index %zu: %s", j, fHistory[j]->GetName());

        RemoveHistoryEntry(j);
      }
      if (j >= 0) RemoveHistoryEntry(j);
      break;
    }
  }

  NLogDebug("Adding history entry: %s", entry->GetName());
  fHistory.push_back(entry);
  Print();
}

bool NGnHttpServer::RemoveHistoryEntry(int index)
{

  if (index < 0 || index >= static_cast<int>(fHistory.size())) {
    NLogError("Invalid history entry index: %d", index);
    return false;
  }

  NGnHistoryEntry * entry  = fHistory[index];
  json              jsonIn = entry->GetConfig();
  json              jsonOut;
  NLogDebug("Removing history entry: %s", entry->GetName());
  NLogDebug("Config: %s", jsonIn.dump().c_str());
  NLogDebug("Invoking HTTP handler for DELETE on entry: %s", entry->GetName());
  fHttpHandlers[entry->GetName()]("DELETE", jsonIn, jsonOut, fObjectsMap);
  delete entry;

  fHistory.erase(fHistory.begin() + index);
  return true;
}

void NGnHttpServer::ClearHistory()
{
  NLogDebug("Clearing %zu history entries.", fHistory.size());
  for (auto entry : fHistory) {
    delete entry;
  }
  fHistory.clear();
}

bool NGnHttpServer::LoadHistoryFromFile(const std::string & filename)
{
  NLogInfo("Loading history from file: %s", filename.c_str());
  json cfg;
  return NUtils::LoadJsonFile(cfg, filename);
}

bool NGnHttpServer::ExportHistoryToFile(const std::string & filename) const
{
  NLogInfo("Exporting history to file: %s", filename.c_str());
  json historyJson = json::array();
  for (const auto & entry : fHistory) {
    json entryJson;
    entryJson["name"]   = entry->GetName();
    entryJson["method"] = "POST";
    entryJson["config"] = entry->GetConfig();
    historyJson.push_back(entryJson);
  }

  bool rc = NUtils::SaveRawFile(filename, historyJson.dump());
  if (rc)
    NLogInfo("Successfully exported history to file: %s", filename.c_str());
  else
    NLogError("Failed to export history to file: %s", filename.c_str());
  return rc;
}

} // namespace Ndmspc
