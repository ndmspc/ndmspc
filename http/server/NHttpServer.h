#ifndef NdmspcCoreNHttpServer_H
#define NdmspcCoreNHttpServer_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
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
#include "ndmspc/http/NRequestIdentity.h"
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
 *
 * A tool may also declare what has to have run before it: the actions named in
 * dependsOn are enforced (a call whose prerequisite has not run is refused
 * naming it) and are used to order `tools/list`, so a group reads in the order
 * its tools have to be used. When two tools share a prerequisite, `order`
 * sequences them (lower first):
 *
 * \code
 *   Ndmspc::RegisterMcpTool("ngnt/reshape", {
 *       .description = "Reshape the opened tree.",
 *       .dependsOn   = {"ngnt/open"},
 *       .order       = 2,
 *       .label       = "{{ binningName }} ({{ levels }})",
 *   });
 * \endcode
 *
 * `label` names a node in the combination tree: the template is filled from the node's own
 * arguments (`{{ arg }}`), so a macro says what its step reads as rather than the framework
 * guessing at the first string argument.
 */
struct NMcpToolInfo {
  std::string              description{};  ///< Human-readable tool description
  std::string              title{};        ///< Optional MCP "title" (defaults to the action name)
  std::vector<std::string> methods{};      ///< Allowed HTTP verbs; empty = all four
  bool                     hidden{false};  ///< Exclude this action from MCP entirely
  json                     inputSchema{};  ///< Optional extra input-schema properties merged in
  std::vector<std::string> dependsOn{};    ///< Actions that must have run first (e.g. "ngnt/open")
  int                      order{0};       ///< Tie-break among ready tools (lower first; 0 = default)
  std::string              label{};        ///< Node name template, e.g. "{{ binningName }} ({{ levels }})"
  /// Whether this action defines session state and is replayed when an idle room is restored (its
  /// opened file, the steps that rebuild what it was doing). Declared here, by the tool, so the room
  /// session needs no list of tool names: an action a group adds is replayed by saying so itself.
  bool                     session{false};
  /// Whether a step is run with a Run button at all. A step whose form *is* how it works - `browse`, whose
  /// tree draws an object on a click - leaves nothing for one, and the client offers none. Declared here,
  /// by the tool, rather than by the client keeping a list of actions it knows.
  bool                     runButton{true};
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
///       macro such as toolNgnt.C includes this header.
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
class NActionWorker;
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
   * @brief Whether the server believes the identity an authenticating front door forwards.
   *
   * The X509 (mutual TLS) front door terminates TLS and verifies the client certificate itself, then
   * forwards the result to this engine as X-Ndmspc-User / -Subject / -Email request headers, because
   * the engine behind it serves anonymously. Enable this only where that door is the only way in (it
   * forwards to a loopback address): anywhere else such a header is something any client can write,
   * and believing it would let a caller name itself.
   *
   * @param trust True when this engine sits behind an authenticating front door.
   */
  void SetTrustForwardedIdentity(bool trust) { fTrustForwardedIdentity = trust; }

  /**
   * @brief Set the heartbeat interval (ms). Recreates timer if running.
   * @param ms Interval in milliseconds. If <=0, heartbeat is disabled.
   */
  void SetHeartbeatMs(int ms);
  /// @brief The interval the deployment configured, which a caller can put back after asking for a
  ///        finer one (see {@link SetHeartbeatMs} and the `heartbeat` action).
  int GetHeartbeatDefaultMs() const { return fHeartbeatDefaultMs; }
  /**
   * @brief Get the current heartbeat interval (ms).
   */
  int GetHeartbeatMs() const { return fHeartbeatMs; }

  /// Notes an action in flight (see the busy-action guard in the action path): while one runs, the room
  /// takes its readings at a fine interval, so a long job can be watched as it goes.
  void SetBusyAction(bool busy);

  /// Notes that the room was asked to do something — any action, over any transport.
  ///
  /// This and {@link SetBusyAction} are what the heartbeat thread reads to choose its cadence, and it is
  /// deliberately not a request the cadence is changed by: an action runs *inline on the worker*, so a
  /// request asking for the finer cadence could not be served until the action it was about had already
  /// finished.
  void NoteActivity();

  /// How many actions are in flight (a nested dispatch — the MCP tool-call path, a room replay — counts
  /// too). Atomic: written by whatever runs the action, read by the heartbeat thread.
  std::atomic<int> fBusyActions{0};

  /// When the room was last asked to do something (see {@link NoteActivity}). Atomic for the same reason.
  std::atomic<std::chrono::steady_clock::time_point> fLastActivity{std::chrono::steady_clock::now()};

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

  /**
   * @brief Processes a request as a given caller.
   *
   * The same dispatch as ProcessRequest, with the caller stated instead of derived from the request:
   * used by the MCP transport, which answers a tool call by dispatching a *synthetic* request of its
   * own (with no headers, and nothing that could carry the identity of the client that asked).
   *
   * @param arg Shared pointer to THttpCallArg containing request data.
   * @param identity The caller the action runs as.
   */
  void ProcessRequestAs(std::shared_ptr<THttpCallArg> arg, const NRequestIdentity & identity);

  /**
   * @brief Runs one action on the action worker.
   *
   * Actions run on one worker thread (not the ROOT request thread) so a cancel frame can still be read
   * while a long action runs. A synchronous caller (plain HTTP, a room replay, an MCP tool call) passes
   * an \p onComplete that signals it and waits; the websocket bridge passes one that sends the reply and
   * returns at once. When it is already called on the worker thread (a handler dispatching another
   * request - the MCP path) the action runs inline, so it cannot deadlock waiting on itself.
   *
   * @param arg The request to run.
   * @param identity The caller to run as, or null to derive it from the request.
   * @param requestId The client's request id, so it can be cancelled; "" when it cannot.
   * @param onComplete Called on the worker thread when the action is done (and the reply is ready).
   */
  void RunAction(std::shared_ptr<THttpCallArg> arg, std::shared_ptr<NRequestIdentity> identity,
                 const std::string & requestId, const std::function<void()> & onComplete);

  /**
   * @brief Asks the action registered under a request id to stop.
   * @param requestId The client's request id.
   * @return True when an action was waiting under that id (running or still queued).
   */
  bool CancelRequest(const std::string & requestId);

  /**
   * @brief Handles a control request inline on this thread.
   *
   * The control requests are `POST /api/cancel` (stop a running action), `POST /api/upload`
   * (bring a file into the room, chunk by chunk), `GET /api/files` (list them), `GET /api/download`
   * (read one back) and `POST /api/delete` (remove one). They are answered here rather than through
   * {@link RunAction}: the worker is busy with the very action being cancelled, so a cancel would
   * queue behind it. The request thread is free (the action runs on the worker), which is what makes
   * a cancel arrive while the action runs. The websocket bridge is not used for this: ROOT delivers a
   * connection's frames only after its previous request has been answered, so a websocket cancel
   * would arrive too late.
   *
   * @param arg The request.
   * @return True when it was a control request and has been answered.
   */
  bool HandleControlRequest(const std::shared_ptr<THttpCallArg> & arg);

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
  /**
   * @brief The workspace schemas of the **current session** — what a tool's `ctx.Workspace()` is.
   *
   * A tool's live defaults (`open`, `reshape`, …) belong to the session they were made in, so two
   * sessions of the same tool do not share one `open` default. Outside a request (the current session
   * is "") this is an empty bucket; `SessionState` sends every session's schemas itself.
   */
  json &                                        GetWorkspace() { return fWorkspaceBySession[fCurrentSession]; }
  /// @brief Get the mutable workspace state JSON.
  json &                                        GetState() { return fWorkspace.GetState(); }
  /// @brief Get the mutable combination tree JSON (see Ndmspc::NInstanceTree).
  json &                                        GetCombinations() { return fWorkspace.GetCombinations(); }
  /// @brief The combination node this request runs for ("" when it is not a node action).
  const std::string &                           GetCurrentInstance() const { return fCurrentInstance; }
  /**
   * @brief The session this request runs in: the root of the combination it belongs to.
   *
   * A session is one open file and the steps under it, so its identity is the root node of the
   * combination. It is what per-session runtime state is keyed by - the objects a tool creates and
   * the workspace defaults it publishes - so several sessions of the same tool can be live at once.
   * "" when the request names no combination.
   */
  const std::string &                           GetCurrentSession() const { return fCurrentSession; }

  /**
   * @brief The session state a client needs to render it, as one websocket frame.
   *
   * A client that has just connected has to be told what the room already holds — the combination
   * tree and the workspace schema (whose `default`s the forms start from) — or its view would be
   * empty until the next action, even though the room has combinations. The shape is the `ngnt`
   * frame the dispatch broadcast uses, so a client reads it with the same handler.
   *
   * @return The frame, or a frame carrying an empty tree/schema when nothing has run yet.
   */
  json                                          SessionState();
  /// @brief Get the combined inspector schema for the workspace.
  json                                          GetInspectorSchema() const { return fWorkspace.GetInspectorSchema(); }
  /**
   * @brief The flat workspace schema the inspector is built from.
   *
   * {@link GetInspectorSchema} - and so the MCP tool schemas - reads this view: every session's and
   * group's published schema, keyed by property name, as an action keeps it in sync with the
   * per-session buckets. It is the mutable counterpart of the read-only inspector schema.
   *
   * @return The flat workspace schema JSON.
   */
  json &                                        GetInspectorWorkspace() { return fWorkspace.GetWorkspace(); }
  /// @brief Set the group prefix used for workspace routes.
  void                                          SetGroup(const std::string & group) { fGroup = group; }
  /// @brief Get the group prefix used for workspace routes.
  const std::string &                           GetGroup() const { return fGroup; }

  /**
   * @brief The pads each tool group has drawn, kept so they can be shown again.
   *
   * A draw is broadcast to whoever is connected and then gone: a client that joins afterwards, and a
   * view that is switched to another tool group, would otherwise show empty pads. This is the room's
   * own record - one entry per (pad, tab label) per group, so re-drawing replaces rather than stacks -
   * sent to a joining client by {@link SessionState}.
   *
   * @return The record, keyed by tool group.
   */
  json &                                        GetPads() { return fPads; }

  /**
   * @brief The room's sessions: one entry per open file, and which one is active.
   *
   * A session is a combination (its root node) - one open file and the steps under it - so this is
   * the root list with each root's group and label, plus the room's active session id. It is what the
   * `session` action answers and what a joining client is handed (see SessionState).
   *
   * @return `{"session": "<id>", "sessions": [{"id","group","label"}, …]}`.
   */
  json                                          SessionList();
  /**
   * @brief Make one session the room's active one (every client follows it).
   * @param session The session's id (its root node).
   * @return False when no such session exists.
   */
  bool                                          ActivateSession(const std::string & session);
  /**
   * @brief Rename one session (what a picker shows it as), keeping what it is.
   * @param session The session's id (its root node).
   * @param name The new name; empty is refused (a session with no name reads as nothing).
   * @return False when no such session exists.
   */
  bool                                          RenameSession(const std::string & session,
                                                            const std::string & name);
  /**
   * @brief The room's own session state, apart from the tree: the names it holds and what each session
   *        has drawn.
   *
   * Both belong to the room rather than to any node, so neither is in the combination snapshot: they
   * ride beside it (see RoomSessionSnapshot) and come back the same way (see AdoptSessionState).
   */
  json                                          SessionStateSnapshot() const;
  /**
   * @brief Take back what {@link SessionStateSnapshot} describes.
   *
   * For a room that has just restored its combinations - the nodes keep their ids, so the names still
   * belong to the right sessions.
   */
  void                                          AdoptSessionState(const json & state);
  /**
   * @brief Start a fresh session for a tool group: a root of the group's first action, carrying that
   *        action's own defaults and a token, and marked as not yet run.
   *
   * Its first step fills it rather than growing a sibling beside it (see FreshSessionRoot), so several
   * sessions of one group can hold the same file without replacing one another.
   *
   * @param group The tool group to start a session for.
   * @param name What the room should call it; empty derives one from the group.
   * @return The new session's id, or "" when the group has no session-defining action.
   */
  std::string                                   StartSession(const std::string & group,
                                                            const std::string & name);
  /**
   * @brief The fresh session a run of `action` fills, if there is one: the session the room is on, while
   *        it is this action's group's own and has nothing in its arguments but its token.
   * @param action The action about to run.
   * @return Its node id, or "" when the run starts a session of its own.
   */
  std::string                                   FreshSessionRoot(const std::string & action);
  /**
   * @brief The name a session of `group` should carry: the group and which of its sessions it is
   *        ("browser 1", "browser 2"), never one another of that group's sessions already has.
   *
   * Every session is named, however it was started - a plain `open` included - so a picker never has to
   * fall back on reading a file path.
   *
   * @param exclude The session being named, when it already exists (it is not one of its own siblings).
   */
  std::string                                   SessionNameFor(const std::string & group,
                                                              const std::string & exclude = "");
  /**
   * @brief The session a tool group is on: its own live chain's root, else its first session.
   * @param group The tool group.
   * @return Its session id, or "" when the group has none.
   */
  std::string                                   GroupSession(const std::string & group);
  /**
   * @brief Remember the pad envelopes one session drew, replacing the tabs it has drawn before.
   * @param key The session the drawing belongs to, or its group when the request names no session.
   * @param envelopes The frame's `payload.pad`: one envelope, or a list of them.
   */
  void                                          RecordPads(const std::string & group, const json & envelopes);

  /// @brief Enable or disable the MCP endpoint (POST /api/mcp). Disabled by default.
  void SetMcpEnabled(bool enabled) { fMcpEnabled = enabled; }
  /// @brief Whether the MCP endpoint (POST /api/mcp) is enabled.
  bool IsMcpEnabled() const { return fMcpEnabled; }

  /// @brief The access tokens this room was started with (empty when it was given none).
  json RoomAccessTokens() const;
  /// @brief Whether this server is a room that was given access tokens, and so enforces them.
  bool RoomAccessRequired() const;
  /// @brief The level a presented token grants here: "rw", "ro", or "" when it grants nothing.
  std::string RoomAccessLevel(const std::string & token) const;
  /// @brief The access token a request carries: `?token=`, the header, or the room's cookie.
  std::string RequestAccessToken(THttpCallArg * arg) const;
  /// @brief This room's own read-write access token ("" when it was given none).
  std::string RoomAccessToken() const;
  /**
   * @brief The router endpoint a room reports its session to.
   *
   * The room presents its own access token there, because the router checks it: the channel is
   * internal, so a room authenticates as itself rather than as a user (see
   * NRoomRouter::HandleState). A room that was given no tokens carries none, exactly as it
   * enforces nothing on its own API.
   *
   * @param base The router's base URL (NDMSPC_ROOM_STATE_URL).
   * @param token The room's read-write token, or "" when it has none.
   * @return The full URL, with the token in the query when there is one.
   */
  static std::string RoomStateUrl(const std::string & base, const std::string & token);
  /**
   * @brief A request header's value, matched without regard to letter case.
   *
   * A proxy in front of this server may re-case a header name: Knative's queue-proxy is a Go
   * process that speaks HTTP/2 and rewrites every name into its canonical form, so a name this
   * server sent as one spelling can arrive as another. ROOT's own THttpCallArg::GetRequestHeader
   * compares the name case-sensitively, so the request's header list is walked here and each name
   * is compared without regard to case instead.
   *
   * @param arg The request (a null pointer yields "").
   * @param name The header name to look for, in any letter case.
   * @return The header's value, or "" when the request does not carry it.
   */
  static std::string RequestHeader(THttpCallArg * arg, const std::string & name);
  /**
   * @brief The deployment's `VITE_*` settings, as a JSON object.
   *
   * A page's settings are normally inlined by Vite at build time, which is why a deployment
   * cannot change them from its environment. This is what lets it: the names the process was
   * started with that begin with `VITE_` - the same prefix Vite itself exposes to the client,
   * and so public by construction - are collected here and handed to the page (see
   * {@link InjectRuntimeEnv}). The rest of the environment (NDMSPC_*, and anything else) is
   * internal and is never emitted.
   *
   * @return The settings, or an empty object when the process has none.
   */
  static json RuntimeEnv();

  /**
   * @brief JSON encoded so it is safe inside an inline `<script>`.
   *
   * The values are deployment settings rather than user input, but they are written into the
   * page: a value carrying `</script>` (or `<`, `>`, `&`, or the U+2028/U+2029 line terminators)
   * must not be able to close the element or break the script. JSON's `\uXXXX` escapes are legal
   * inside a JSON string, so the characters are replaced by them.
   *
   * @param value The value to encode.
   * @return The JSON text, safe to place inside `<script>...</script>`.
   */
  static std::string JsonForHtml(const json & value);

  /**
   * @brief The page with the deployment's settings injected, or "" when there are none.
   *
   * The settings are written as `window.__NDMSPC_ENV__` ahead of the page's own scripts, so a
   * module that reads a setting while it loads already sees it.
   *
   * @param html The page as it was built.
   * @param env The settings to inject (see {@link RuntimeEnv}).
   * @return The page to serve.
   */
  static std::string InjectRuntimeEnv(const std::string & html, const json & env);
  /**
   * @brief Enforce a room's access tokens on one request.
   *
   * A room that was given tokens serves nothing without one: a missing or unknown token is
   * refused, and a read-only token may only issue GETs. Nothing is enforced when this server is
   * not a room, or was given no tokens - an older image, or a room created before access existed.
   *
   * @param arg The request.
   * @param method The request's HTTP method.
   * @param isPage True for the page/static path, false for an `/api` one.
   * @param alreadyAdmitted True for a request the server dispatched itself (see ProcessRequestAs),
   *        which the caller's own request already carried this room's token for - or which has no
   *        client behind it at all, as a replay does.
   * @return True when the request may be served; otherwise the refusal is already written.
   */
  bool ApplyRoomAccess(THttpCallArg * arg, const std::string & method, bool isPage,
                       bool alreadyAdmitted = false);

  /**
   * @brief One request, dispatched to its handler with the caller it runs as.
   *
   * Both entry points end here, so the identity a handler sees is decided in exactly one place:
   * `statedIdentity` when the caller knew it (the MCP transport), otherwise the one the request
   * itself carries - a verified token, a forwarded front-door header, or nothing at all.
   *
   * @param arg The request.
   * @param statedIdentity The caller to run as, or null to work it out from the request.
   */
  void Dispatch(std::shared_ptr<THttpCallArg> arg, const NRequestIdentity * statedIdentity);

  protected:
  /**
   * @brief The first declared prerequisite of an action that has not run.
   *
   * A tool declares its prerequisites in NMcpToolInfo::dependsOn; they are enforced before its
   * handler runs, so a call whose prerequisite has not run is refused naming the action to run
   * first instead of failing on a missing object. The workspace history - the record of the
   * actions that ran successfully - is what says whether a prerequisite is met, so this needs no
   * state of its own.
   *
   * @param action The handler action about to run (e.g. "ngnt/reshape").
   * @return The unmet prerequisite's action, or "" when all are met (or none is declared).
   */
  std::string UnmetPrerequisite(const std::string & action) const;

  /**
   * @brief Make one group's live session exactly the given combination path.
   *
   * A group has **one** live combination (one NGnTree, one navigator, one open file per group); other
   * groups' live combinations are left alone, so the browser's file stays open while the analysis
   * group works. This truncates the group's live chain where the path diverges from it and replays the
   * remaining nodes' POST handlers with their stored params, so the objects, the workspace schema and
   * the history all describe that combination. A node that cannot be materialized is reported in
   * @p out and returns false, leaving the request unserved.
   *
   * @param path The node ids from a root down to the node to make live.
   * @param group The group the path belongs to (its live chain is the one that moves).
   * @param out The response, filled with the reason when materialization fails.
   * @return True when the path is live.
   */
  bool MaterializeCombination(const std::vector<std::string> & path, const std::string & group, json & out);

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

  /**
   * @brief Reads the default page and prepares the copy that carries this deployment's settings.
   *
   * Called as the engine is about to serve (StartEngine), which is after the page was set and
   * before any request: the page is read, the settings are injected (see
   * {@link InjectRuntimeEnv}) and the result is what a page request is answered with. Nothing is
   * prepared - and THttpServer serves the page exactly as before - when the process carries no
   * `VITE_*` setting, or the page is not one to touch.
   */
  void PrepareRuntimeEnvPage();

  protected:
  NWsHandler *      fNWsHandler{nullptr}; ///<! WebSocket handler instance
  std::shared_ptr<IOidcTokenVerifier> fOidcVerifier; ///<! Shared OIDC token verifier (HTTP + WS)
  bool              fTrustForwardedIdentity{false}; ///<! Whether the front door's identity headers are believed
  bool              fWsEnabled{false};   ///<! Whether WebSocket support was requested
  bool              fEngineStarted{false}; ///<! Whether the HTTP engine has been created
  std::chrono::seconds fAuthenticationTimeout{15}; ///<! WS authentication timeout
  int               fHeartbeatMs{10000};  ///<! Heartbeat interval in milliseconds
  int               fHeartbeatDefaultMs{10000}; ///<! The interval it was configured with, kept so a caller can restore it
  std::thread *     fHeartbeatThread{nullptr}; ///<! Background heartbeat thread
  std::atomic<bool> fHeartbeatRunning{false};  ///<! Whether the heartbeat thread is running

  NActionWorker *   fActionWorker{nullptr};    ///<! The one thread tool actions run on
  std::mutex        fWorkerMutex;              ///<! Guards fActionWorker's creation
  /// The cancel flag of each action waiting to run, by the client's request id. Set by CancelRequest;
  /// the worker checks it before running a queued action and the loops poll it while one runs.
  std::mutex        fCancelMutex;              ///<! Guards fCancelFlags
  std::map<std::string, std::shared_ptr<std::atomic<bool>>> fCancelFlags;
  std::atomic<int> fServCnt{0};           ///<! Service counter used in heartbeat payload
  std::mutex        fHeartbeatMutex;       ///<! Guards the heartbeat interval/timer
  std::condition_variable fHeartbeatCv;    ///<! Signals heartbeat thread wake-up/shutdown
  std::mutex             fHeartbeatCvMutex; ///<! Mutex paired with fHeartbeatCv

  mutable std::mutex                            fHandlersMutex;    ///<! Guards fHttpHandlers
  std::map<std::string, Ndmspc::NHttpFuncPtr> fHttpHandlers;       ///<! HTTP handlers map
  std::map<std::string, TObject *>              fObjectsMap;         ///<! Objects map for handlers
  std::string                                   fCurrentInstance;    ///<! Combination node this request targets ("" = none)
  std::string                                   fCurrentSession;     ///<! Root of the combination this request belongs to ("" = none)
  NWorkspace                                  fWorkspace{nullptr}; ///<! Workspace object (TNamed)
  bool fUseHistory{true};  ///<! Flag to indicate whether to use history in processing requests
  bool fMcpEnabled{false}; ///<! Flag to indicate whether the MCP endpoint (/api/mcp) is enabled
  std::string fGroup;      ///<! Group prefix for workspace routes
  json        fPads = json::object(); ///<! group -> array of the pad envelopes that group has drawn
  /// The workspace schemas each group has published, kept apart: the flat map collides (two groups
  /// both publish "open"), so a joining client is sent this instead (see SessionState).
  std::map<std::string, json> fWorkspaceByGroup;
  /// The workspace schemas of each session (one open file and its steps), which is what a tool's
  /// `ctx.Workspace()` reads and writes: two sessions of a tool keep their own defaults.
  std::map<std::string, json> fWorkspaceBySession;
  /// What each session is called, as the room holds it. A name belongs to the room - beside its group
  /// and its view - rather than to any tool's parameters: a session is whatever a group started with
  /// (an `open`, or any other first step), so what it is called cannot come from one of them.
  std::map<std::string, std::string> fSessionNames;
  std::string fRuntimeEnvPage; ///<! The page with this deployment's VITE_* injected ("" = serve the built page)

  mutable std::mutex fRoomMutex;              ///<! Guards the room-session fields below
  std::string        fRoomId;                 ///<! NDMSPC_ROOM: set when this server is a room
  std::string        fRoomStateUrl;           ///<! NDMSPC_ROOM_STATE_URL: the router to report to
  // Copy-initialised on purpose: `json fRoomAccess{json::object()}` would call the initializer-list
  // constructor and store `[{}]` - a *non-empty* array, which read as "this room enforces access"
  // on every server, router included.
  json               fRoomAccess = json::object(); ///<! NDMSPC_ROOM_ACCESS: the tokens to enforce
  /// NDMSPC_ROOM_MAX_FILE_SIZE / NDMSPC_ROOM_MAX_STORAGE: the ceilings a room puts on one upload and
  /// on its working directory. Parsed once at construction; 0 means unlimited. Set by the room
  /// deployment, not by the client, so the limit holds even against a hand-written request.
  long long          fMaxFileBytes{0};        ///<! Largest single file the room accepts (0 = no cap)
  long long          fMaxStorageBytes{0};     ///<! Largest total the room's directory may hold (0 = no cap)
  std::string        fRoomPushed;             ///<! Last reported snapshot, to skip no-op reports
  bool               fRoomRestoring{false};   ///<! Guards the nested replay against recursion
  bool               fRoomRestored{false};    ///<! Nothing left to restore
  long               fRoomRestoreNextTrySec{0}; ///<! Cooldown before retrying a failed fetch

  /// \cond CLASSIMP
  ClassDefOverride(NHttpServer, 3);
  /// \endcond;
};

/// @brief Global pointer to the most recently constructed NHttpServer instance.
extern NHttpServer * gNHttpServer;

} // namespace Ndmspc
#endif
