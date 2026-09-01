#ifndef NdmspcNWsHandler_H
#define NdmspcNWsHandler_H
#include <map>    // For std::map
#include <string> // For std::string
#include <mutex>  // For std::mutex
#include <chrono> // For std::chrono::system_clock
#include <cstdio>

#include <THttpWSHandler.h>
#include <TString.h>
#include <THttpCallArg.h>  // For THttpCallArg from ROOT

#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NWsClientInfo.h" // Include our client info class in the same namespace


class THttpCallArg;
class TTimer;
namespace Ndmspc {

/**
 * @class NWsHandler
 * @brief Handles WebSocket connections and messaging for NDMSPC.
 *
 * Inherits from THttpWSHandler to manage WebSocket events, broadcast messages,
 * and maintain client information in a thread-safe manner.
 *
 * ### WS_DATA API request/reply protocol
 * Incoming `WS_DATA` messages are treated as HTTP API calls when the payload parses as a
 * JSON object containing a non-empty `path`. They are converted to a THttpCallArg and routed
 * through `NGnHttpServer::ProcessRequest`, i.e. the same handler that would serve
 * `POST /api/<path>`. Any message that is not valid JSON or lacks `path` falls back to the
 * legacy chat-relay demo (broadcast to other clients).
 *
 * Request (sent by client over the websocket):
 * @code
 * {
 *   "requestId": "optional-client-id",     // optional, any JSON value, echoed back verbatim
 *   "method": "POST",                      // optional, default "POST" (or "GET"/"DELETE")
 *   "path": "group/action",                // required, same path used for HTTP /api/<path>
 *   "query": "k=v&...",                    // optional, raw query string
 *   "payload": { }                         // optional, becomes the JSON POST body
 * }
 * @endcode
 *
 * Reply (sent back only to the requesting client):
 * @code
 * {
 *   "event": "ngnt_reply",
 *   "requestId": "optional-client-id",     // echoed from the request, or null
 *   "contentType": "application/json",     // arg->GetContentType() from ProcessRequest
 *   "payload": { }                         // parsed JSON response body, or raw string
 * }
 * @endcode
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NWsHandler : public THttpWSHandler {
  public:
  /**
   * @brief Constructor.
   * @param name Optional handler name.
   * @param title Optional handler title.
   */
  NWsHandler(const char * name = nullptr, const char * title = nullptr);

  /**
   * @brief Destructor.
   */
  ~NWsHandler() override;

  size_t GetClientCount() const;

  /**
   * @brief Returns the default page content for the handler.
   * @return Default page content string.
   */
  TString GetDefaultPageContent() override { return "file:ws.htm"; }

  /**
   * @brief Processes a WebSocket event and broadcasts messages to clients.
   * @param arg Pointer to THttpCallArg containing event data.
   * @return True if processed successfully.
   */
  Bool_t ProcessWS(THttpCallArg * arg) override;

  /**
   * @brief Broadcasts a message to all connected clients (unsafe, not thread-safe).
   * @param message Message string to broadcast.
   */
  void BroadcastUnsafe(const std::string & message);

  /**
   * @brief Broadcasts a message to all connected clients (thread-safe).
   * @param message Message string to broadcast.
   */
  void Broadcast(const std::string & message);

  /**
   * @brief Handles timer events for the handler.
   * @param timer Pointer to TTimer object.
   * @return True if handled successfully.
   */
  Bool_t HandleTimer(TTimer * timer) override;

  protected:
  std::map<ULong_t, NWsClientInfo> fClients;    ///< Map of active clients by ID
  std::mutex                       fMutex;      ///< Mutex for thread-safe client map access
  Int_t                            fServCnt{0}; ///< Service counter
  std::chrono::system_clock::time_point fServerStartedAt; ///< Server start time
  // network stats snapshot for computing speeds
  json                             fPrevNetStats; ///< previous network counters snapshot
  std::chrono::steady_clock::time_point fPrevNetTs; ///< timestamp of previous snapshot
  bool                             fHavePrevNet{false}; ///< whether previous snapshot exists
  // TFile IO stats snapshot for computing speeds
  json                             fPrevFileStats; ///< previous TFile IO counters snapshot
  std::chrono::steady_clock::time_point fPrevFileTs; ///< timestamp of previous file snapshot
  bool                             fHavePrevFile{false}; ///< whether previous file snapshot exists

  /// \cond CLASSIMP
  ClassDefOverride(NWsHandler, 1);
  /// \endcond;
};

} // namespace Ndmspc
#endif
