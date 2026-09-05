
#include <chrono>
#include <thread>
#include <condition_variable>
#include <memory>
#include <utility>

#include <TString.h>
#include <TSystem.h>
#include <THttpCallArg.h>
#include <THttpServer.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"

#include "ndmspc/http/NCloudEvent.h"
#include "NHttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHttpServer);
/// \endcond

namespace Ndmspc {

NHttpServer::NHttpServer(const char * engine, bool ws, int heartbeat_ms, NOidcConfig oidcConfig, bool startEngine)
    : THttpServer(startEngine ? engine : ""), fWsEnabled(ws), fHeartbeatMs(heartbeat_ms), fHeartbeatThread(nullptr)
{
  const auto authenticationTimeout = oidcConfig.authenticationTimeout;
  fAuthenticationTimeout = authenticationTimeout;

  // Build the shared OIDC token verifier once. The same verifier guards both
  // the WebSocket connections and the plain HTTP /api requests. When no OIDC
  // issuer/audience is configured the server stays in anonymous mode.
  if (oidcConfig.Enabled()) {
    auto authenticator = std::make_shared<NKeycloakOidcAuthenticator>(std::move(oidcConfig));
    authenticator->Initialize();
    fOidcVerifier = std::move(authenticator);
  }

  if (startEngine) {
    // THttpServer(engine) above already created the engine; finish the
    // WebSocket setup and heartbeat now.
    SetupWebSocketAndHeartbeat();
  }
}

bool NHttpServer::StartEngine(const char * engine)
{
  // Idempotent: nothing to do when an engine is already running.
  if (fEngineStarted || IsAnyEngine()) {
    fEngineStarted = true;
    SetupWebSocketAndHeartbeat();
    return IsAnyEngine();
  }
  if (!engine || !*engine) return false;

  // Create the engine (starts civetweb listening). When the engine fails to
  // bind (e.g. port in use) no engine is added and IsAnyEngine() is false.
  if (!CreateEngine(engine)) return false;
  fEngineStarted = true;
  SetupWebSocketAndHeartbeat();
  return IsAnyEngine();
}

void NHttpServer::SetupWebSocketAndHeartbeat()
{
  if (fWsEnabled && !fNWsHandler) {
    fNWsHandler = new NWsHandler("ws", "ws", fOidcVerifier, fAuthenticationTimeout);
    Register("/", fNWsHandler);
  }
  if (fHeartbeatMs > 0 && fNWsHandler && !fHeartbeatThread) StartHeartbeatThread();
}

void NHttpServer::SetHeartbeatMs(int ms)
{
  std::lock_guard<std::mutex> lk(fHeartbeatMutex);
  fHeartbeatMs = ms;
  // restart thread according to new interval
  StopHeartbeatThread();
  if (fNWsHandler && fHeartbeatMs > 0) StartHeartbeatThread();
}

NHttpServer::~NHttpServer()
{
  StopHeartbeatThread();
}

void NHttpServer::StartHeartbeatThread()
{
  if (fHeartbeatThread || fHeartbeatMs <= 0) return;
  fHeartbeatRunning.store(true);
  fHeartbeatThread = new std::thread([this]() {
    std::unique_lock<std::mutex> lk(fHeartbeatCvMutex);
    while (fHeartbeatRunning.load()) {
      // wait_for returns when notified or when timeout elapses
      auto dur = std::chrono::milliseconds(fHeartbeatMs);
      // release fHeartbeatCvMutex while waiting but will reacquire on wake
      fHeartbeatCv.wait_for(lk, dur, [this]() { return !fHeartbeatRunning.load(); });
      if (!fHeartbeatRunning.load()) break;
      try {
        // Delegate to NWsHandler's timer handler so it updates snapshots (file/net) and broadcasts
        if (fNWsHandler) {
          fNWsHandler->HandleTimer(nullptr);
        } else {
          // fallback: simple heartbeat
          json data = json::object();
          data["event"] = "heartbeat";
          json payload = json::object();
          payload["count"] = ++fServCnt;
          payload["clients"] = 0;
          data["payload"] = payload;
          WebSocketBroadcast(data);
        }
      } catch (...) {
        // swallow errors to keep thread alive
      }
    }
  });
}

void NHttpServer::StopHeartbeatThread()
{
  if (!fHeartbeatThread) return;
  fHeartbeatRunning.store(false);
  // Wake up the sleeping heartbeat thread immediately
  fHeartbeatCv.notify_all();
  if (fHeartbeatThread->joinable()) fHeartbeatThread->join();
  delete fHeartbeatThread;
  fHeartbeatThread = nullptr;
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
