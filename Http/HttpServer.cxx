#include <TString.h>
#include <TSystem.h>
#include <THttpCallArg.h>
#include <cstring>
#include <THttpServer.h>
#include <Rtypes.h>

#include "CloudEvent.h"
#include "HttpServer.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::HttpServer);
/// \endcond

namespace NdmSpc {

HttpServer::HttpServer(const char * engine) : THttpServer(engine) {}

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

} // namespace NdmSpc
