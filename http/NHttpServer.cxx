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

} // namespace Ndmspc
