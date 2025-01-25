#include <TString.h>
#include <TSystem.h>
#include <THttpCallArg.h>
#include <cstring>
#include <THttpServer.h>
#include <Rtypes.h>

#include "CloudEvent.h"
#include "WebSocketHandler.h"
#include "HttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::HttpServer);
/// \endcond

namespace Ndmspc {

HttpServer::HttpServer(const char * engine, bool ws) : THttpServer(engine)
{
  WebSocketHandler * handler = new WebSocketHandler("ws", "ws");
  Register("/", handler);
}

void HttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{

  CloudEvent ce(arg.get());
  if (ce.IsValid()) {
    ProcessCloudEventRequest(&ce, arg);
  }
  THttpServer::ProcessRequest(arg);
}
void HttpServer::ProcessCloudEventRequest(CloudEvent * ce, std::shared_ptr<THttpCallArg> arg)
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
