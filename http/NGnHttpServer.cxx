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
  json in;
  try {
    in = json::parse((const char *)arg->GetPostData());
  }
  catch (json::parse_error & e) {
    NLogError("JSON parse error: %s", e.what());
    arg->SetContentType("application/json");
    arg->SetContent("{\"error\": \"Invalid JSON format\"}");
    return;
  }
  NLogInfo("Received %s request with content: %s", method.Data(), in.dump().c_str());

  if (in["action"].is_null()) {
    NLogError("Missing 'action' field in request");
    arg->SetContentType("application/json");
    arg->SetContent("{\"error\": \"Missing 'action' field\"}");
    return;
  }

  std::string action = in["action"].get<std::string>();
  if (fHttpHandlers.find(action) == fHttpHandlers.end()) {
    NLogError("Unsupported action: %s", action.c_str());
    arg->SetContentType("application/json");
    arg->SetContent("{\"error\": \"Unsupported action\"}");
    return;
  }

  fObjectsMap["httpServer"] = this;

  json out;
  fHttpHandlers[action](method.Data(), in, out, fObjectsMap);

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
