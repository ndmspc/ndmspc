#include <TString.h>
#include <TSystem.h>
#include <THttpCallArg.h>
#include <cstring>
#include <THttpServer.h>
#include <Rtypes.h>

#include "NCloudEvent.h"
#include "NHttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHttpServer);
/// \endcond

namespace Ndmspc {

NHttpServer::NHttpServer(const char * engine, bool ws) : THttpServer(engine)
{
  if (ws) {
    fNWsHandler = new NWsHandler("ws", "ws");
    Register("/", fNWsHandler);
  }
}

void NHttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{

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
