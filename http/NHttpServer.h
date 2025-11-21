#ifndef NdmspcCoreNHttpServer_H
#define NdmspcCoreNHttpServer_H

#include <THttpServer.h>
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

  protected:
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

  private:
  NWsHandler * fNWsHandler{nullptr}; ///< WebSocket handler instance

  /// \cond CLASSIMP
  ClassDef(NHttpServer, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
