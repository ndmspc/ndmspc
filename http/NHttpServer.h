#ifndef NdmspcCoreNHttpServer_H
#define NdmspcCoreNHttpServer_H

#include <THttpServer.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "NCloudEvent.h"
#include "NWsHandler.h"

class THttpCallArg;
namespace Ndmspc {

/**
 * @class NHttpServer
 * @brief HTTP server class for Ndmspc, supporting WebSocket and CloudEvent handling.
 *
 * NHttpServer extends THttpServer to provide HTTP and WebSocket server functionality,
 * including request processing and CloudEvent integration. It manages a NWsHandler
 * for WebSocket connections and offers customizable heartbeat and engine options.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NHttpServer : public THttpServer {
  public:
  /**
   * @brief Constructs a new NHttpServer instance.
   * @param engine Engine specification string (default: "http:8080").
   * @param ws Enable WebSocket support (default: true).
   * @param heartbeat_ms Heartbeat interval in milliseconds (default: 10000).
   */
  NHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000);

  /**
   * @brief Gets the WebSocket handler.
   * @return Pointer to NWsHandler instance.
   */
  NWsHandler * GetWebSocketHandler() const { return fNWsHandler; }
  bool         WebSocketBroadcast(json message);

  /**
   * @brief Set the heartbeat interval (ms). Recreates timer if running.
   * @param ms Interval in milliseconds. If <=0, heartbeat is disabled.
   */
  void SetHeartbeatMs(int ms);
  /**
   * @brief Get the current heartbeat interval (ms).
   */
  int GetHeartbeatMs() const { return fHeartbeatMs; }

  /**
   * @brief Destructor stops background heartbeat thread if running.
   */
  virtual ~NHttpServer();

  protected:
  /**
   * @brief Start the background heartbeat thread (internal).
   */
  void StartHeartbeatThread();

  /**
   * @brief Stop the background heartbeat thread (internal).
   */
  void StopHeartbeatThread();

  protected:
  NWsHandler *      fNWsHandler{nullptr}; ///<! WebSocket handler instance
  int               fHeartbeatMs{10000};  ///<! Heartbeat interval in milliseconds
  std::thread *     fHeartbeatThread{nullptr};
  std::atomic<bool> fHeartbeatRunning{false};
  std::atomic<int> fServCnt{0};           ///<! Service counter used in heartbeat payload
  std::mutex        fHeartbeatMutex;
  std::condition_variable fHeartbeatCv;
  std::mutex             fHeartbeatCvMutex;

  /**
   * @brief Processes an HTTP request.
   * @param arg Shared pointer to THttpCallArg containing request data.
   */
  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg);

  /**
   * @brief Processes a CloudEvent HTTP request.
   * @param ce Pointer to NCloudEvent instance.
   * @param arg Shared pointer to THttpCallArg containing request data.
   */
  virtual void ProcessNCloudEventRequest(NCloudEvent * ce, std::shared_ptr<THttpCallArg> arg);

  /// \cond CLASSIMP
  ClassDef(NHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
