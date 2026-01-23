#include <TTimer.h>
#include "NLogger.h"
#include "NGnHttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnHttpServer);
/// \endcond

namespace Ndmspc {
NGnHttpServer::NGnHttpServer(const char * engine, bool ws, int heartbeat_ms) : NHttpServer(engine, ws, heartbeat_ms)
{
  if (ws) {
    fNWsHandler = new NGnWsHandler("ws", "ws");
    Register("/", fNWsHandler);
    TTimer * heartbeat = new TTimer(fNWsHandler, heartbeat_ms);
    heartbeat->Start();
  }
}

void NGnHttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{

  NLogInfo("NGnHttpServer::ProcessRequest");

  // json out;
  // out["msg"] = "Hello from ndmspc-cli";
  arg->AddHeader("X-Header", "Test");
  // arg->SetContent(out.dump());
  // arg->SetContentType("application/json");
  arg->SetContent("Success");
  arg->SetContentType("text/plain");
}
} // namespace Ndmspc
