#ifndef Ndmspc_NRoomClient_H
#define Ndmspc_NRoomClient_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ndmspc/core/NLogger.h"      ///< provides the global `json` (nlohmann) alias
#include "ndmspc/http/NHttpRequest.h" ///< NHttpResponse
#include "ndmspc/http/NRoomAccess.h"  ///< NRoomAccess: the token names and levels

namespace Ndmspc {

/**
 * @struct NRoomInfo
 * @brief One room as reported by the router's room/list action.
 */
struct NRoomInfo {
  std::string name;            ///< Kubernetes resource name (prefix + slug of the room id)
  std::string room;            ///< The room id, as chosen by the client
  std::string revision;        ///< Latest ready Knative revision
  long        lastSeen{0};     ///< Epoch seconds of the last request that touched the room
  bool        ready{false};    ///< Knative Service Ready condition
  int         replicas{0};     ///< Running pods of the latest revision (0 when the room is idle)
  bool        active{false};   ///< replicas > 0

  /// The router's own view of the room: "preparing", "pending", "ready", "not ready" or "failed".
  std::string state;
  bool        preparing{false}; ///< Its creation is still running (state is "preparing")
  std::string phase;            ///< Where that creation is: service, ready, route, restore, pending
  std::string error;            ///< Why it is not serving ("" while it is only preparing)
  std::string code;             ///< Stable reason behind `error`, e.g. "no_capacity" ("" when unknown)
  long        startedAt{0};     ///< Epoch seconds its creation started, for the elapsed time

  /// The pod profile the room runs at ("" for a deployment that offers no profiles).
  std::string profile;
  /// What its container requests and is limited to, as Kubernetes spells them ("" when undeclared).
  /// The requests are what the room reserves; the web rooms view shows them the same way.
  std::string cpuRequest;    ///< CPU request
  std::string cpuLimit;      ///< CPU limit
  std::string memoryRequest; ///< Memory request
  std::string memoryLimit;   ///< Memory limit

  /// Why the room's container last died ("" until it has). A room the kernel killed for using more
  /// memory than its limit never says so itself, so the router reads the pod and reports it here.
  std::string lastErrorReason;
  std::string lastErrorMessage;   ///< The router's sentence for that, for a one-line display
  /// -1 when unknown, not 0: 0 is a real exit code (a container that exited cleanly), so "no death
  /// recorded" has to be a value of its own. `NUtils::GetJsonInt` reads an absent member as -1, which
  /// is what these are defaulted to, so an absent `lastError` cannot look like a clean exit.
  int  lastErrorExit{-1};   ///< The exit code it died with
  long lastErrorAt{-1};     ///< Epoch seconds it died (as above, when unknown)

  /// Token that opens the room and may do anything in it ("" when it was given none).
  std::string tokenRw;
  /// Token that opens the room but may only read it ("" when it was given none).
  std::string tokenRo;

  /// Who created the room ("" for one created before ownership existed, or by nobody identifiable).
  std::string owner;

  /// @brief Whether the router reported access tokens for this room.
  bool HasAccess() const { return !tokenRw.empty() || !tokenRo.empty(); }
  /**
   * @brief The token that opens this room at one level.
   * @param level "rw" for the read-write token, anything else for the read-only one.
   * @return The token, or "" when the room has none.
   */
  std::string TokenFor(const std::string & level) const;
};

/**
 * @struct NRoomResult
 * @brief Outcome of a room action.
 */
struct NRoomResult {
  bool        ok{false}; ///< True when the router reported success
  std::string error;     ///< Actionable failure reason when ok is false
  json        payload;   ///< The action's payload when ok is true
};

/**
 * @struct NRoomListResult
 * @brief Outcome of the room/list action.
 */
struct NRoomListResult {
  bool                   ok{false}; ///< True when the router reported success
  std::string            error;     ///< Actionable failure reason when ok is false
  std::vector<NRoomInfo> rooms;     ///< Rooms the router is tracking
  int                    ttl{0};    ///< Idle TTL in seconds, after which a room is deleted
  /**
   * Whether the router answered this caller as an admin (see NDMSPC_ROOM_ADMINS), which is why the
   * list may hold rooms that are not the caller's. False for a caller the router could not identify.
   */
  bool                   admin{false};
};

/**
 * @class IRoomHttpClient
 * @brief Minimal HTTP client used by NRoomClient, so tests can inject a fake
 *        (same shape as IOidcTokenHttpClient).
 */
class IRoomHttpClient {
  public:
  virtual ~IRoomHttpClient() = default;
  /**
   * @brief POST a JSON body to a URL.
   * @param url Absolute request URL (the MCP endpoint).
   * @param body Request body (a JSON-RPC message).
   * @param headers Request headers (Content-Type, and Authorization when a token is set).
   * @return The raw HTTP response.
   */
  virtual NHttpResponse Post(const std::string & url, const std::string & body,
                             const std::map<std::string, std::string> & headers) = 0;
};

/**
 * @class NRoomHttpClientImpl
 * @brief httplib-backed transport for the room router's MCP endpoint.
 *
 * Wraps NHttpRequest so the room client reuses the TLS options the other NDMSPC
 * clients expose: a client certificate for mutual TLS, a CA bundle or directory to
 * verify the server, and an opt-out of server verification.
 */
class NRoomHttpClientImpl : public IRoomHttpClient {
  public:
  /**
   * @brief Constructor.
   * @param certFile Client certificate (PEM) for mutual TLS (optional).
   * @param keyFile Client private key (PEM) for mutual TLS (optional).
   * @param keyPasswordFile Base64-encoded file holding the private-key passphrase (optional).
   * @param caFile CA bundle used to verify the server certificate (optional).
   * @param caPath CA directory (hashed certificates) used to verify the server (optional).
   * @param insecure If true, the server certificate is not verified.
   */
  NRoomHttpClientImpl(std::string certFile = "", std::string keyFile = "", std::string keyPasswordFile = "",
                      std::string caFile = "", std::string caPath = "", bool insecure = false);

  /**
   * @brief POST a JSON body to a URL.
   * @param url Absolute request URL.
   * @param body Request body.
   * @param headers Request headers.
   * @return The raw HTTP response.
   * @throws std::runtime_error when the transport fails (no HTTP response at all).
   */
  NHttpResponse Post(const std::string & url, const std::string & body,
                     const std::map<std::string, std::string> & headers) override;

  private:
  NHttpRequest fHttp;            ///< Transport used for every call
  std::string  fCertFile;        ///< Client certificate (mTLS)
  std::string  fKeyFile;         ///< Client private key (mTLS)
  std::string  fKeyPasswordFile; ///< Base64-encoded private-key passphrase file
  std::string  fCaFile;          ///< CA bundle used to verify the server
  std::string  fCaPath;          ///< CA directory used to verify the server
  bool         fInsecure{false}; ///< When true, the server certificate is not verified
};

/**
 * @class NRoomClient
 * @brief Drives the room router through its MCP endpoint (POST {url}/api/mcp).
 *
 * The router is the ngnt server with the room macro loaded
 * (Ndmspc::NRoomRouter). Its four room actions are exposed as the MCP tools
 * `room_list`, `room_open`, `room_status` and `room_close`, which dispatch to the
 * same handlers as the /api/room routes. Going through MCP tools/call keeps every room
 * operation on one interface instead of a second, REST-shaped one.
 *
 * The room id always travels in the request body, never as a `?room=` query
 * parameter: once a room's HTTPRoute exists the gateway routes that query to the
 * room itself rather than to the router.
 *
 * No method of this class throws; failures are reported through the result structs.
 */
class NRoomClient {
  public:
  /**
   * @brief Constructor.
   * @param endpoint MCP endpoint URL, e.g. "http://localhost:8080/api/mcp".
   * @param bearerToken OIDC access token sent as `Authorization: Bearer ...` (optional).
   * @param httpClient Injectable transport; a default httplib-backed one is created when null.
   */
  NRoomClient(std::string endpoint, std::string bearerToken = "",
              std::shared_ptr<IRoomHttpClient> httpClient = nullptr);

  /**
   * @brief Derive the MCP endpoint from a server URL.
   * @param url A server base URL ("http://host:8080") or a URL already ending in "/api/mcp".
   * @return The MCP endpoint to POST JSON-RPC messages to.
   */
  static std::string McpEndpoint(const std::string & url);

  /**
   * @brief Perform the MCP handshake, so an unreachable or misconfigured router is
   *        reported before a UI starts.
   * @param error Filled with an actionable message when the handshake fails.
   * @return True when the endpoint answered a valid MCP initialize.
   */
  bool Initialize(std::string & error);

  /**
   * @brief The owner this client acts as, sent with every room request.
   *
   * The router believes a verified identity - a token it checked itself, or the certificate a mutual
   * TLS front door checked - and only falls back to what a client asserts, so this is what makes a
   * room belong to someone where there is no login, and what limits this client to the rooms that
   * are theirs. An empty owner (the default) says nothing about itself, which is what an operator's
   * script wants: every room, every action.
   *
   * @param owner An email address or a user name.
   */
  void SetOwner(const std::string & owner) { fOwner = owner; }

  /**
   * @brief List the rooms the router is tracking.
   * @return The rooms, the idle TTL, and any failure.
   */
  NRoomListResult List();

  /**
   * @brief A `room_list` payload in the shape {@link List} returns.
   *
   * The router pushes the same payload down the websocket (a watcher asks for the list over the
   * socket and is sent it whenever it changes), and a pushed list has to be read exactly as an
   * answered one: a field the router leaves out when it is empty must mean the same thing either
   * way, or a view that watched and a view that polled would disagree about the same room.
   *
   * @param payload The payload of a `room_list` answer, or of a pushed `rooms` event.
   * @return The rooms, the idle TTL and whether the caller is an admin; `ok` is false when the
   *         payload is not a room list.
   */
  static NRoomListResult ParseList(const json & payload);

  /**
   * @brief Ensure a room exists, creating its Knative Service when it does not.
   *
   * With `wait` the router returns only once the room is ready, which can take tens of seconds
   * (NDMSPC_ROOM_READY_TIMEOUT, 45s by default) and - because the router serves one request at a
   * time - keeps every other caller waiting too. Without it the router registers the room and
   * answers at once with `state` = "preparing", leaving the creation to a background thread; the
   * caller then follows it with Status() until the state stops being "preparing".
   *
   * @param roomId Room id (any client-chosen string).
   * @param wait Whether to wait for the room to be ready before answering.
   * @return The action result; payload holds room, name, revision, param, url, ttl and state
   *         (plus phase while it is still preparing, `state=pending` with `code=no_capacity` - and
   *         the scheduler's message in `error` - when the cluster has no room for the room's pod
   *         yet, or `error` when it failed).
   */
  NRoomResult Open(const std::string & roomId, bool wait = true);

  /**
   * @brief Report one room's state.
   *
   * This is also how a client follows a room that is being prepared: while `state` is
   * "preparing" the payload carries `phase` and `startedAt`, and it ends as "ready",
   * "pending" (waiting for cluster resources, with `code=no_capacity` and the scheduler's message),
   * "not ready" or "failed" (with `error`).
   *
   * @param roomId Room id.
   * @return The action result; payload holds room, name, param, tracked, revision,
   *         lastSeen, exists and ready.
   */
  NRoomResult Status(const std::string & roomId);

  /**
   * @brief Delete a room's HTTPRoute and Knative Service.
   * @param roomId Room id.
   * @return The action result; payload holds room and name.
   */
  NRoomResult Close(const std::string & roomId);

  /**
   * @brief Export every tracked room and its session to a document.
   *
   * The document is the payload: the room set plus each room's session, suitable for writing to a
   * file and restoring later or elsewhere. It carries no ROOT data - only which file was open,
   * its navigator and its drill-down.
   *
   * @return The action result; payload holds the document.
   */
  NRoomResult Backup();

  /**
   * @brief Ensure every room in a document and replay its session.
   *
   * Additive and convergent by default: rooms are created (or rolled) from the current skeleton and
   * their sessions replayed, rooms not named in the document are untouched, and a room that already
   * has a file open is left alone. With `replace`, a room the document names that is already there is
   * deleted first, so the document's session is what it comes back holding. The payload holds
   * `restored` and `failed` lists, so a partial restore is visible rather than fatal.
   *
   * @param document A document produced by Backup().
   * @param replace Delete the rooms the document names that already exist before restoring them.
   * @return The action result.
   */
  NRoomResult Restore(const json & document, bool replace = false);

  private:
  /**
   * @brief Invoke one room tool through MCP tools/call.
   * @param tool MCP tool name (e.g. "room_list").
   * @param method HTTP verb the handler expects ("GET", "POST" or "DELETE").
   * @param roomId Room id; omitted from the arguments when empty.
   * @param extra Extra arguments merged in beside "method" and "room" (e.g. the restore document).
   * @return The action result.
   */
  NRoomResult Call(const std::string & tool, const std::string & method, const std::string & roomId,
                   const json & extra = json());

  /**
   * @brief POST a JSON-RPC message and return the raw response body.
   * @param message The JSON-RPC message.
   * @param error Filled with an actionable message on transport or HTTP failure.
   * @param body Filled with the response body on success.
   * @return True when a 200 response was received.
   */
  bool Post(const json & message, std::string & error, std::string & body);

  /**
   * @brief Parse a JSON-RPC tools/call response into a room result.
   * @param body The response body.
   * @return The action result.
   */
  NRoomResult ParseCallResponse(const std::string & body) const;

  std::string                      fEndpoint;    ///< MCP endpoint URL
  std::string                      fBearerToken; ///< Optional OIDC access token
  std::string                      fOwner;       ///< Optional asserted owner, sent with every request
  std::shared_ptr<IRoomHttpClient> fHttpClient;  ///< Transport used for every call
  long long                        fNextId{0};   ///< JSON-RPC request id counter
};
} // namespace Ndmspc
#endif
