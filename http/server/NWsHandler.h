#ifndef NdmspcNWsHandler_H
#define NdmspcNWsHandler_H
#include <map>    // For std::map
#include <string> // For std::string
#include <mutex>  // For std::mutex
#include <chrono> // For std::chrono::system_clock
#include <condition_variable> // For std::condition_variable
#include <set>    // For std::set
#include <cstdio>
#include <memory>
#include <vector>

#include "ndmspc/http/NOidcAuthenticator.h"

#include <THttpWSHandler.h>
#include <TString.h>
#include <THttpCallArg.h>  // For THttpCallArg from ROOT

#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NWsClientInfo.h" // Include our client info class in the same namespace


class THttpCallArg;
class TTimer;
namespace Ndmspc {

/**
 * @brief Runtime state of a WebSocket connection awaiting authentication.
 */
struct NWsPendingClient {
  std::chrono::steady_clock::time_point readyAt; ///< Deadline for completing authentication
  bool authenticationInProgress{false};          ///< Whether an authentication message is being processed
};

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
 * through `NHttpServer::ProcessRequest`, i.e. the same handler that would serve
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
 *   "payload": { },                        // optional, becomes the JSON POST body
 *   "headers": { "X-Custom": "value" }     // optional, forwarded as request headers (string values only)
 * }
 * @endcode
 *
 * Reply (sent back only to the requesting client):
 * @code
 * {
 *   "event": "message_reply",
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
   * @param verifier Shared OIDC token verifier (nullptr = anonymous mode).
   * @param authenticationTimeout Budget for completing the authentication handshake.
   */
  NWsHandler(const char * name = nullptr, const char * title = nullptr,
             std::shared_ptr<IOidcTokenVerifier> verifier = nullptr,
             std::chrono::seconds authenticationTimeout = std::chrono::seconds(15));

  /**
   * @brief Destructor.
   */
  ~NWsHandler() override;

  /// @brief Get the number of currently connected clients.
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

  /// @brief Allow the reply to be sent from the action worker thread (the async WS bridge).
  Bool_t AllowMTSend() const override { return kTRUE; }

  /// @brief Called by ROOT when a multi-threaded send finishes (releases the connection's send slot).
  void CompleteWSSend(UInt_t wsId) override;

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
   * @brief The ids of the clients currently connected, a still-valid token each.
   *
   * An action that pushes to watchers (the room router's rooms list) walks these and answers each
   * client in its own right, so what a client is sent is filtered for the caller it is.
   *
   * @return The connection ids, empty when nobody is connected.
   */
  std::vector<ULong_t> ConnectedIds() const;

  /**
   * @brief The identity a connection was admitted as.
   * @param wsId WebSocket client id.
   * @return The username the connection authenticated with, or "" when it was not identified (an
   *         anonymous deployment, or a client that has not authenticated yet).
   */
  std::string UsernameOf(ULong_t wsId) const;

  /**
   * @brief Sends one message to one client.
   * @param wsId WebSocket client id.
   * @param message Message string to send.
   * @return True when the message was handed to the connection.
   */
  bool SendTo(ULong_t wsId, const std::string & message);

  /**
   * @brief Handles timer events for the handler.
   * @param timer Pointer to TTimer object.
   * @return True if handled successfully.
   */
  Bool_t HandleTimer(TTimer * timer) override;

  protected:
  /**
   * @brief Process an incoming "authenticate" message for a client.
   * @param wsId WebSocket client id.
   * @param message Raw authenticate message (carries the token).
   */
  void HandleAuthentication(ULong_t wsId, const json & message);
  /**
   * @brief Register a client in anonymous mode and welcome it.
   * @param wsId WebSocket client id.
   */
  void ActivateAnonymousClient(ULong_t wsId);
  /**
   * @brief Send the "welcome" frame to a client and broadcast the client list.
   * @param wsId WebSocket client id.
   * @param username Username to welcome with.
   */
  void SendWelcomeAndAnnounce(ULong_t wsId, const std::string & username);
  /**
   * @brief Send an authentication error frame to a client.
   * @param wsId WebSocket client id.
   * @param code Stable error code.
   * @param message Human-readable error message.
   * @param retryable Whether the client may retry authentication.
   */
  void SendAuthenticationError(ULong_t wsId, const std::string & code, const std::string & message, bool retryable);
  /**
   * @brief Remove a client and broadcast the updated client list.
   * @param wsId WebSocket client id.
   */
  void RemoveClientAndAnnounce(ULong_t wsId);
  /**
   * @brief Admit an upgrading connection against the room's access tokens.
   *
   * A room that was given tokens serves nothing without one, and a websocket does not go through
   * the HTTP gate - so the upgrade has to carry the token in its URL (what a page link passes on)
   * or in the room's cookie. The level the token grants is remembered for the connection, which is
   * how a read-only one is kept to GETs over the API bridge.
   *
   * @param arg The upgrade request.
   * @return True when the connection may be established.
   */
  bool ApplyRoomAccessToUpgrade(THttpCallArg * arg);
  /**
   * @brief The room-access level a connection was admitted with.
   * @param wsId WebSocket client id.
   * @return "rw", "ro", or "" when this connection needed no token.
   */
  std::string AccessLevelOf(ULong_t wsId) const;
  /// @brief Drop clients whose token expired and pending clients past the auth timeout.
  void ExpireConnections();
  /// @brief Build the "clients" broadcast payload.
  json BuildClientsMessage() const;
  /// @brief Get the ids of connected clients with a still-valid token.
  std::vector<ULong_t> ClientIds() const;

  /**
   * @brief Sends one frame to one connection, serialized against the previous send.
   *
   * ROOT's websocket engine permits one send in flight per connection; a send attempted before the
   * previous completed is dropped ("Try to book next send operation before previous completed"). An
   * action run on the worker may send two frames in a row (the state broadcast and then the reply),
   * so every send goes through here and waits for the previous one to finish ({@link CompleteWSSend}).
   *
   * @param wsId The connection.
   * @param message The frame text.
   * @return True when the frame was handed to the engine.
   */
  bool SendFrame(ULong_t wsId, const std::string & message);

  std::map<ULong_t, NWsClientInfo> fClients;    ///< Map of active clients by ID
  std::map<ULong_t, NWsPendingClient> fPendingClients; ///<! Runtime pending authentication state
  std::map<ULong_t, std::string>   fAccessLevels; ///<! Room-access level per admitted connection
  mutable std::mutex               fMutex;      ///<! Mutex for thread-safe client map access
  /// Serializes sends: ROOT's engine allows one in flight per connection, so a send waits for the
  /// previous one to complete ({@link CompleteWSSend}) — the worker can otherwise send the state
  /// broadcast and the reply back-to-back and have the second dropped.
  std::mutex                       fSendMutex;  ///<! Guards fSending
  std::condition_variable          fSendCv;     ///<! Signals a completed send
  std::set<ULong_t>                fSending;    ///<! Connection ids with a send in flight
  std::shared_ptr<IOidcTokenVerifier> fOidcVerifier; ///<! Runtime token verifier
  std::chrono::seconds fAuthenticationTimeout; ///<! Runtime authentication timeout
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
  ClassDefOverride(NWsHandler, 2);
  /// \endcond;
};

} // namespace Ndmspc
#endif
