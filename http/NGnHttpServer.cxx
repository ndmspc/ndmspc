#include <NGnTree.h>
#include <TTimer.h>
#include "NHttpServer.h"
#include "NLogger.h"
#include "NGnHttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnHttpServer);
/// \endcond

namespace Ndmspc {
NGnHttpServer::NGnHttpServer(const char * engine, bool ws, int heartbeat_ms) : NHttpServer(engine, ws, heartbeat_ms) {}

void NGnHttpServer::Print(Option_t * option) const
{
  NHttpServer::Print(option);
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

  fObjectsMap["httpServer"] = this;

  json out;
  fHttpHandlers[fullpath.Data()](method.Data(), in, out, fObjectsMap);

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

} // namespace Ndmspc
