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

  TString method = arg->GetMethod();
  if (arg->GetRequestHeader("Content-Type").CompareTo("application/json")) {
    // NLogWarning("Unsupported Content-Type: %s", arg->GetRequestHeader("Content-Type").Data());
    NHttpServer::ProcessRequest(arg);
    return;
  }

  std::string path     = arg->GetPathName();
  std::string filename = arg->GetFileName();

  std::string fullpath = path + "/" + filename;

  // remove first slash if exists
  fullpath.erase(0, fullpath.find_first_not_of('/'));
  // remove last slash if exists
  fullpath.erase(std::remove(fullpath.end() - 1, fullpath.end(), '/'), fullpath.end());

  std::string query = arg->GetQuery();
  NLogDebug("Processing %s request for path: %s query: %s", method.Data(), fullpath.c_str(), query.c_str());

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

  if (fHttpHandlers.find(fullpath) == fHttpHandlers.end()) {
    NLogError("Unsupported action: %s", fullpath.c_str());
    arg->SetContentType("application/json");
    arg->SetContent("{\"error\": \"Unsupported action\"}");
    return;
  }

  fObjectsMap["httpServer"] = this;

  json out;
  fHttpHandlers[fullpath](method.Data(), in, out, fObjectsMap);

  // out["status"] = "ok";

  arg->AddHeader("X-Header", "Test");
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
