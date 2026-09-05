#ifndef NdmspcCoreNHttpServer_H
#define NdmspcCoreNHttpServer_H

#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>

#include <THttpServer.h>

#include "ndmspc/http/NCloudEvent.h"
#include "ndmspc/http/NOidcConfig.h"
#include "ndmspc/http/NWsHandler.h"

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
   * @param oidcConfig OIDC configuration (empty disables authentication).
   * @param startEngine When false the engine is not started yet; call
   *        StartEngine() once initialization (e.g. HTTP handler registration)
   *        is complete. This avoids serving requests before the server is
   *        fully set up, which can race with handler-map population.
   */
  NHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000,
              NOidcConfig oidcConfig = {}, bool startEngine = true);

  /**
   * @brief (Re)start the HTTP engine with the given specification.
   *
   * Intended for servers constructed with startEngine=false: after all
   * handlers are registered the engine is created here, which starts
   * listening and (for WebSocket servers) the heartbeat thread.
   *
   * @param engine Engine specification string, e.g. "http:8080?top=ndmspc".
   * @return True when an engine is now running.
   */
  bool StartEngine(const char * engine);

  /**
   * @brief Gets the WebSocket handler.
   * @return Pointer to NWsHandler instance.
   */
  NWsHandler * GetWebSocketHandler() const { return fNWsHandler; }
  bool         WebSocketBroadcast(json message);

  /**
   * @brief Gets the shared OIDC token verifier (may be null in anonymous mode).
   *
   * The same verifier guards both WebSocket and HTTP requests.
   */
  std::shared_ptr<IOidcTokenVerifier> GetOidcVerifier() const { return fOidcVerifier; }

  /**
   * @brief Whether OIDC authentication is enabled for this server.
   */
  bool OidcEnabled() const { return static_cast<bool>(fOidcVerifier); }

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

  /**
   * @brief Create the WebSocket handler and start the heartbeat (internal).
   *
   * Called from the constructor when the engine starts immediately, or from
   * StartEngine() when construction was deferred.
   */
  void SetupWebSocketAndHeartbeat();

  protected:
  NWsHandler *      fNWsHandler{nullptr}; ///<! WebSocket handler instance
  std::shared_ptr<IOidcTokenVerifier> fOidcVerifier; ///<! Shared OIDC token verifier (HTTP + WS)
  bool              fWsEnabled{false};   ///<! Whether WebSocket support was requested
  bool              fEngineStarted{false}; ///<! Whether the HTTP engine has been created
  std::chrono::seconds fAuthenticationTimeout{15}; ///<! WS authentication timeout
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
