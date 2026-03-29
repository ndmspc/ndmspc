#include <TString.h>
#include <TSystem.h>
#include <THttpCallArg.h>
#include <THttpServer.h>

#include <chrono>
#include <thread>

#include "NCloudEvent.h"
#include "NHttpServer.h"
#include "NLogger.h"
#include "NUtils.h"
#include <condition_variable>

/// \cond CLASSIMP
ClassImp(Ndmspc::NHttpServer);
/// \endcond

namespace Ndmspc {

NHttpServer::NHttpServer(const char * engine, bool ws, int heartbeat_ms) : THttpServer(engine), fHeartbeatMs(heartbeat_ms), fHeartbeatThread(nullptr)
{
  if (ws) {
    fNWsHandler = new NWsHandler("ws", "ws");
    Register("/", fNWsHandler);
    if (fHeartbeatMs > 0) StartHeartbeatThread();
  }
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
