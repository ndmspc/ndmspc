#include <TString.h>
#include <TSystem.h>
#include <THttpCallArg.h>
#include <THttpServer.h>

#include "NCloudEvent.h"
#include "NHttpServer.h"
#include "NLogger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHttpServer);
/// \endcond

namespace Ndmspc {

NHttpServer::NHttpServer(const char * engine, bool ws, int heartbeat_ms) : THttpServer(engine)
{
  if (ws) {
    fNWsHandler = new NWsHandler("ws", "ws");
    Register("/", fNWsHandler);
    TTimer * heartbeat = new TTimer(fNWsHandler, heartbeat_ms);
    heartbeat->Start();
  }
  // // check if server is running
  // if (!IsRunning()) {
  //   NLogError("NHttpServer: Server is not running on engine %s", engine);
  // } else {
  //   NLogInfo("NHttpServer: Server is running on engine %s", engine);
  // }
}

void NHttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{

  // NLogInfo("NHttpServer::ProcessRequest");
  NCloudEvent ce(arg.get());
  if (ce.IsValid()) {
    NHttpServer::ProcessNCloudEventRequest(&ce, arg);
  }
  THttpServer::ProcessRequest(arg);
}
void NHttpServer::ProcessNCloudEventRequest(NCloudEvent * ce, std::shared_ptr<THttpCallArg> arg)
{

  arg->SetTextContent(TString::Format("Success : %s", ce->GetInfo().c_str()).Data());
  // json out;
  // out["msg"] = "Hello from ndmspc-cli";
  // arg->AddHeader("X-Header", "Test");
  // arg->SetContent(out.dump());
  // arg->SetContentType("application/json");
  // arg->SetContent("Success");
  // arg->SetContentType("text/plain");
}
bool NHttpServer::WebSocketBroadcast(json message)
{
  NLogTrace("Broadcasting message to all clients.");
  if (fNWsHandler) {
    std::string msgStr = message.dump();
    fNWsHandler->BroadcastUnsafe(msgStr);
    return true;
  }
  return false;
}

} // namespace Ndmspc
