#ifndef NdmspcCoreNHttpServer_H
#define NdmspcCoreNHttpServer_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <THttpServer.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/http/NWorkspace.h"
#include "ndmspc/http/NOidcConfig.h"
#include "ndmspc/http/NWsHandler.h"

class THttpCallArg;
namespace Ndmspc {

/**
 * @brief Function pointer type for HTTP handlers.
 *
 * The handler function takes the following parameters:
 * - std::string: The HTTP request path or identifier.
 * - json&: Reference to the input JSON payload.
 * - json&: Reference to the output JSON payload.
 * - json&: Reference to the output JSON payload to websocket.
 */
using NHttpFuncPtr = void (*)(std::string, json &, json &, json &, std::map<std::string, TObject *> &);

/**
 * @brief Map of HTTP handler names to their corresponding function pointers.
 */
using NHttpHandlerMap = std::map<std::string, NHttpFuncPtr>;

/**
 * @brief Global pointer to the HTTP handler map.
 */
extern NHttpHandlerMap * gNdmspcHttpHandlers;

/**
 * @brief Decides whether a websocket upgrade may be served by this server.
 *
 * Set by the room router (Ndmspc::NRoomRouter), which is not a client endpoint: a connection
 * that reaches it either carries no `?room=<id>` or names a room it does not know, and answering it
 * would hand the client a session with no data in it. Answering kFALSE refuses the upgrade.
 *
 * Null on any server without rooms, which then accepts every websocket as it always has.
 *
 * @param query The request's query string, without the leading '?', or an empty string.
 * @return kTRUE to accept the upgrade, kFALSE to refuse it.
 */
using NdmspcWsConnectFilter = Bool_t (*)(const std::string & query);

/// @brief The websocket-upgrade filter described above; null when the server has no policy.
extern NdmspcWsConnectFilter gNdmspcWsConnectFilter;

/**
 * @brief MCP metadata for one registered HTTP handler action.
 *
 * Handler macros declare this alongside the handler itself so the Model Context
 * Protocol layer (NMcpServer) can describe the action without hardcoding strings
 * in C++:
 *
 * \code
 *   Ndmspc::RegisterMcpTool("ngnt/open", {
 *       .description = "Open or close an NGnTree ROOT file.",
 *       .methods     = {"GET", "POST", "DELETE"},
 *   });
 * \endcode
 */
struct NMcpToolInfo {
  std::string              description{};  ///< Human-readable tool description
  std::string              title{};        ///< Optional MCP "title" (defaults to the action name)
  std::vector<std::string> methods{};      ///< Allowed HTTP verbs; empty = all four
  bool                     hidden{false};  ///< Exclude this action from MCP entirely
  json                     inputSchema{};  ///< Optional extra input-schema properties merged in
};

/// @brief Map of handler action (e.g. "ngnt/open") to its MCP metadata.
using NMcpToolMap = std::map<std::string, NMcpToolInfo>;

/// @brief Global pointer to the MCP metadata map, set by the CLI before macros load.
extern NMcpToolMap * gNdmspcMcpTools;

/// @brief Register (or replace) the MCP metadata for a handler action.
/// @note No-op when gNdmspcMcpTools is null, so macros loaded outside a wired CLI
///       (e.g. by ndmspc-run) do not crash.
inline void RegisterMcpTool(const std::string & action, NMcpToolInfo info)
{
  if (gNdmspcMcpTools != nullptr) (*gNdmspcMcpTools)[action] = std::move(info);
}

/// @brief Convenience overload for the common case of a description only.
/// @note The parameter is templated (rather than a plain std::string) so that a
///       brace-enclosed NMcpToolInfo literal - e.g. `{ .description = "...", .methods = {"GET"} }` -
///       never becomes ambiguous with this overload: GCC 11 accepts such a list as a
///       conversion to std::string, while a template parameter is not deduced from it.
/// @note The std::is_convertible SFINAE constraint is deliberate: a C++20 `requires`
///       clause would be rejected by the ROOT interpreter (cling runs in C++17) when a
///       macro such as httpNgnt.C includes this header.
template <typename T, std::enable_if_t<std::is_convertible_v<T, std::string>, int> = 0>
inline void RegisterMcpTool(const std::string & action, const T & description)
{
  NMcpToolInfo info;
  info.description = description;
  RegisterMcpTool(action, std::move(info));
}

///
/// \class NHttpServer
///
/// \brief The Ndmspc HTTP server.
///
/// It owns the HTTP engine and the WebSocket handler, the workspace with its request
/// history, the macro handler map (actions registered by the macros a deployment loads),
/// the MCP endpoint and the room session: a room reports its session to the router while
/// it lives and replays it when it wakes again.
///
/// \author Martin Vala <mvala@cern.ch>
///
class NHistoryEntry;
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
  /**
   * @brief Broadcast a message to all connected WebSocket clients.
   * @param message JSON message to broadcast.
   * @return True when the broadcast succeeded.
   */
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

  /// @brief Print server information.
  /// @param option Optional ROOT option string (unused).
  virtual void Print(Option_t * option = "") const override;
  /// @brief Clear the server state.
  /// @param option Optional ROOT option string (unused).
  virtual void Clear(Option_t * option = "") override { THttpServer::Clear(option); }
  /// @brief Clear the workspace history.
  void         ClearHistory() { fWorkspace.Clear(); }
  /// @brief Clear the workspace history and remove all remaining input objects.
  void         ResetServer();

  /**
   * @brief Destructor stops background heartbeat thread if running.
   */
  virtual ~NHttpServer();

  /// @brief Enable or disable keeping a request history in the workspace.
  /// @param useHistory New flag value.
  void         SetUseHistory(bool useHistory) { fUseHistory = useHistory; }
  /// @brief Whether the request history is kept in the workspace.
  bool         GetUseHistory() const { return fUseHistory; }

  /// @brief Get the workspace history entries as a JSON array.
  json GetJson() const;

  /// @brief This server's session as a snapshot, or a null json when nothing is open.
  json RoomSessionSnapshot();

  /// @brief Report this server's session to the room router.
  ///
  /// Only active inside a room (NDMSPC_ROOM_STATE_URL is set). Called after a request that
  /// may have changed the session; it does nothing when no file is open or nothing changed.
  void RoomSessionPush();

  /**
   * @brief Fetch the stored session from the router and replay it, before serving.
   *
   * A room wakes as an empty process, so this brings back the session it had before it
   * scaled to zero. It replays in place (through ProcessRequest) and gives up after a few
   * attempts, so a router that is briefly unreachable cannot cost every request.
   *
   * Called at the start of ProcessRequest, and from the WebSocket path (NWsHandler), so
   * whichever kind of request wakes the room restores it before it is served.
   */
  void RoomSessionRestoreOnce();

  /**
   * @brief Processes an HTTP request.
   *
   * Everything outside /api is served by THttpServer; /api actions go through the handler
   * map registered by the loaded macros.
   *
   * @param arg Shared pointer to THttpCallArg containing request data.
   */
  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  /// @brief Replace the HTTP handler map (thread-safe).
  void SetHttpHandlers(std::map<std::string, Ndmspc::NHttpFuncPtr> handlers);

  /// @brief Copy of the HTTP handler map (thread-safe).
  std::map<std::string, Ndmspc::NHttpFuncPtr> GetHttpHandlers() const;

  /// @brief Look up a handler by path without inserting (thread-safe).
  /// @return The handler function pointer, or nullptr when not registered.
  Ndmspc::NHttpFuncPtr FindHttpHandler(const std::string & name) const;

  /**
   * @brief Register an input object under a name for handlers to use.
   * @param name Object name.
   * @param obj Object pointer (not owned by the map).
   */
  void      AddInputObject(const std::string & name, TObject * obj) { fObjectsMap[name] = obj; }
  /**
   * @brief Remove and delete a registered input object.
   * @param name Object name.
   * @return True when an object was found and removed.
   */
  bool      RemoveInputObject(const std::string & name);
  /**
   * @brief Get a registered input object by name.
   * @param name Object name.
   * @return Pointer to the object, or nullptr when not registered.
   */
  TObject * GetInputObject(const std::string & name);

  /// @brief Get the map of registered input objects.
  std::map<std::string, TObject *> &            GetObjectsMap() { return fObjectsMap; }
  /// @brief Get the mutable workspace schema JSON.
  json &                                        GetWorkspace() { return fWorkspace.GetWorkspace(); }
  /// @brief Get the mutable workspace state JSON.
  json &                                        GetState() { return fWorkspace.GetState(); }
  /// @brief Get the combined inspector schema for the workspace.
  json                                          GetInspectorSchema() const { return fWorkspace.GetInspectorSchema(); }
  /// @brief Set the group prefix used for workspace routes.
  void                                          SetGroup(const std::string & group) { fGroup = group; }
  /// @brief Get the group prefix used for workspace routes.
  const std::string &                           GetGroup() const { return fGroup; }

  /// @brief Enable or disable the MCP endpoint (POST /api/mcp). Disabled by default.
  void SetMcpEnabled(bool enabled) { fMcpEnabled = enabled; }
  /// @brief Whether the MCP endpoint (POST /api/mcp) is enabled.
  bool IsMcpEnabled() const { return fMcpEnabled; }

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
  std::thread *     fHeartbeatThread{nullptr}; ///<! Background heartbeat thread
  std::atomic<bool> fHeartbeatRunning{false};  ///<! Whether the heartbeat thread is running
  std::atomic<int> fServCnt{0};           ///<! Service counter used in heartbeat payload
  std::mutex        fHeartbeatMutex;       ///<! Guards the heartbeat interval/timer
  std::condition_variable fHeartbeatCv;    ///<! Signals heartbeat thread wake-up/shutdown
  std::mutex             fHeartbeatCvMutex; ///<! Mutex paired with fHeartbeatCv

  mutable std::mutex                            fHandlersMutex;    ///<! Guards fHttpHandlers
  std::map<std::string, Ndmspc::NHttpFuncPtr> fHttpHandlers;       ///<! HTTP handlers map
  std::map<std::string, TObject *>              fObjectsMap;         ///<! Objects map for handlers
  NWorkspace                                  fWorkspace{nullptr}; ///<! Workspace object (TNamed)
  bool fUseHistory{true};  ///<! Flag to indicate whether to use history in processing requests
  bool fMcpEnabled{false}; ///<! Flag to indicate whether the MCP endpoint (/api/mcp) is enabled
  std::string fGroup;      ///<! Group prefix for workspace routes

  mutable std::mutex fRoomMutex;              ///<! Guards the room-session fields below
  std::string        fRoomId;                 ///<! NDMSPC_ROOM: set when this server is a room
  std::string        fRoomStateUrl;           ///<! NDMSPC_ROOM_STATE_URL: the router to report to
  std::string        fRoomPushed;             ///<! Last reported snapshot, to skip no-op reports
  bool               fRoomRestoring{false};   ///<! Guards the nested replay against recursion
  bool               fRoomRestored{false};    ///<! Nothing left to restore
  long               fRoomRestoreNextTrySec{0}; ///<! Cooldown before retrying a failed fetch

  /// \cond CLASSIMP
  ClassDefOverride(NHttpServer, 1);
  /// \endcond;
};

/// @brief Global pointer to the most recently constructed NHttpServer instance.
extern NHttpServer * gNHttpServer;

} // namespace Ndmspc
#endif
