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
  NGnHttpServer(const char * engine = "http:8080", bool ws = true, int heartbeat_ms = 10000,
                NOidcConfig oidcConfig = {}, bool startEngine = true);

  virtual void Print(Option_t * option = "") const override;
  virtual void Clear(Option_t * option = "") override { NHttpServer::Clear(option); }
  void         ClearHistory() { fWorkspace.Clear(); }
  void         ResetServer();

  void         SetUseHistory(bool useHistory) { fUseHistory = useHistory; }
  bool         GetUseHistory() const { return fUseHistory; }

  json GetJson() const;

  virtual void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;

  /// @brief Replace the HTTP handler map (thread-safe).
  void SetHttpHandlers(std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers);

  /// @brief Copy of the HTTP handler map (thread-safe).
  std::map<std::string, Ndmspc::NGnHttpFuncPtr> GetHttpHandlers() const;

  /// @brief Look up a handler by path without inserting (thread-safe).
  /// @return The handler function pointer, or nullptr when not registered.
  Ndmspc::NGnHttpFuncPtr FindHttpHandler(const std::string & name) const;

  void      AddInputObject(const std::string & name, TObject * obj) { fObjectsMap[name] = obj; }
  bool      RemoveInputObject(const std::string & name);
  TObject * GetInputObject(const std::string & name);

  std::map<std::string, TObject *> &            GetObjectsMap() { return fObjectsMap; }
  json &                                        GetWorkspace() { return fWorkspace.GetWorkspace(); }
  json &                                        GetState() { return fWorkspace.GetState(); }
  json                                          GetInspectorSchema() const { return fWorkspace.GetInspectorSchema(); }
  void                                          SetGroup(const std::string & group) { fGroup = group; }
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

  /// \cond CLASSIMP
  ClassDefOverride(NGnHttpServer, 1);
  /// \endcond;
};

extern NGnHttpServer * gNGnHttpServer;

} // namespace Ndmspc
#endif
