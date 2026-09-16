#ifndef Ndmspc_NGnHttpServer_H
#define Ndmspc_NGnHttpServer_H
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "ndmspc/core/NLogger.h"
#include "ndmspc/http/NGnWorkspace.h"
#include "NHttpServer.h"

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
using NGnHttpFuncPtr = void (*)(std::string, json &, json &, json &, std::map<std::string, TObject *> &);

/**
 * @brief Map of HTTP handler names to their corresponding function pointers.
 */
using NGnHttpHandlerMap = std::map<std::string, NGnHttpFuncPtr>;

/**
 * @brief Global pointer to the HTTP handler map.
 */
extern NGnHttpHandlerMap * gNdmspcHttpHandlers;

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
inline void RegisterMcpTool(const std::string & action, const std::string & description)
{
  NMcpToolInfo info;
  info.description = description;
  RegisterMcpTool(action, std::move(info));
}

///
/// \class NGnHttpServer
///
/// \brief NGnHttpServer object
///	\author Martin Vala <mvala@cern.ch>
///
class NGnHistoryEntry;
class NGnHistory;
class NGnHttpServer : public NHttpServer {

  public:
  /**
   * @brief Constructor.
   * @param engine Engine specification string (default: "http:8080").
   * @param ws Enable WebSocket support (default: true).
   * @param heartbeat_ms Heartbeat interval in milliseconds (default: 10000).
   * @param oidcConfig OIDC configuration (empty disables authentication).
   * @param startEngine When false the engine is not started yet; call ResetServer() once
   *        handler registration is complete.
   */
  NGnHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000,
                NOidcConfig oidcConfig = {}, bool startEngine = true);

  /// @brief Print server information.
  /// @param option Optional ROOT option string (unused).
  virtual void Print(Option_t * option = "") const override;
  /// @brief Clear the server state.
  /// @param option Optional ROOT option string (unused).
  virtual void Clear(Option_t * option = "") override { NHttpServer::Clear(option); }
  /// @brief Clear the workspace history.
  void         ClearHistory() { fWorkspace.Clear(); }
  /// @brief Clear the workspace history and remove all remaining input objects.
  void         ResetServer();

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

  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  /// @brief Replace the HTTP handler map (thread-safe).
  void SetHttpHandlers(std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers);

  /// @brief Copy of the HTTP handler map (thread-safe).
  std::map<std::string, Ndmspc::NGnHttpFuncPtr> GetHttpHandlers() const;

  /// @brief Look up a handler by path without inserting (thread-safe).
  /// @return The handler function pointer, or nullptr when not registered.
  Ndmspc::NGnHttpFuncPtr FindHttpHandler(const std::string & name) const;

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

  private:
  mutable std::mutex                            fHandlersMutex;    ///<! Guards fHttpHandlers
  std::map<std::string, Ndmspc::NGnHttpFuncPtr> fHttpHandlers;       ///<! HTTP handlers map
  std::map<std::string, TObject *>              fObjectsMap;         ///<! Objects map for handlers
  NGnWorkspace                                  fWorkspace{nullptr}; ///<! Workspace object (TNamed)
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
  ClassDefOverride(NGnHttpServer, 1);
  /// \endcond;
};

/// @brief Global pointer to the most recently constructed NGnHttpServer instance.
extern NGnHttpServer * gNGnHttpServer;

} // namespace Ndmspc
#endif
