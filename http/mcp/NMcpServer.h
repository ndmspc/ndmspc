#ifndef Ndmspc_NMcpServer_H
#define Ndmspc_NMcpServer_H

#include <string>
#include <vector>

#include "ndmspc/core/NLogger.h" ///< provides the global `json` (nlohmann) alias
#include "ndmspc/http/NRequestIdentity.h"

namespace Ndmspc {

class NHttpServer;
struct NMcpToolInfo; ///< defined in NHttpServer.h (handler + MCP registration API)

///
/// \class NMcpServer
/// \brief Exposes the registered NGnTree HTTP handlers as Model Context Protocol
///        (MCP) tools.
///
/// The server is stateless apart from the caller a tool call runs as: it translates JSON-RPC 2.0
/// requests into calls against an in-process NHttpServer. Each `tools/call` is routed through
/// NHttpServer::ProcessRequestAs (the same path used by the HTTP API and the WebSocket bridge, which
/// derives the caller from the request itself), so history, the workspace merge, the WebSocket
/// broadcast and the authentication gate all behave exactly as they do for UI clients.
///
/// Supported JSON-RPC methods: `initialize`, `notifications/initialized`,
/// `tools/list`, `tools/call`, `ping`.
///
class NMcpServer {

  public:
  /// @brief Configuration for the MCP server.
  struct Options {
    std::string              serverName{"ndmspc-ngnt"}; ///< Name reported in the "initialize" result
    std::string              serverVersion; ///< Filled from NDMSPC_VERSION when empty
    std::string              protocolVersion{"2025-06-18"}; ///< MCP protocol version reported to clients
    std::vector<std::string> toolPrefixes;  ///< Empty = every registered handler
    std::vector<std::string> excludeActions{"debug", "openapi/inspector", "inspector/openapi"}; ///< Actions never exposed as tools
    bool                     exposeMethodParam{true}; ///< Expose the HTTP method as a tool parameter
  };

  /**
   * @brief Constructor with default options.
   * @param server Server whose handlers are exposed as MCP tools.
   */
  explicit NMcpServer(NHttpServer * server);
  /**
   * @brief Constructor.
   * @param server Server whose handlers are exposed as MCP tools.
   * @param opts MCP server options.
   */
  explicit NMcpServer(NHttpServer * server, Options opts);

  /// @brief Handle one JSON-RPC message.
  /// @return The JSON-RPC response, or a null json for notifications (no `id`).
  json Handle(const json & message) const;

  /// @brief Parse text then Handle(); a parse failure yields a -32700 error.
  json HandleText(const std::string & text) const;

  /// @brief Build the `tools/list` result (one tool per registered handler).
  json BuildTools() const;

  /// @brief Execute a tool and build the `tools/call` result payload.
  json CallTool(const std::string & toolName, const json & arguments) const;

  /// @brief Map a handler key (e.g. "ngnt/open") to an MCP-safe tool name ("ngnt_open").
  static std::string ToolName(const std::string & handlerKey);

  /// @brief Resolve a tool name back to its registered handler key ("" when unknown).
  std::string FindHandlerKey(const std::string & toolName) const;

  /**
   * @brief The caller every tool call of this instance runs as.
   *
   * A tool call is dispatched as a request of its own, built from the tool's arguments alone, so it
   * carries none of the headers that would say who asked. The transport that owns this instance
   * states the caller instead: the HTTP endpoint sets it from the request it is answering, and the
   * stdio transport - which has no caller - leaves it empty.
   *
   * @param identity The caller (empty for an anonymous one).
   */
  void SetCallerIdentity(const NRequestIdentity & identity) { fCallerIdentity = identity; }

  private:
  /**
   * @brief Whether a handler action is excluded from the tool list.
   * @param handlerKey Handler key (e.g. "ngnt/open").
   * @return True when the action must not be exposed.
   */
  bool                 IsExcluded(const std::string & handlerKey) const;
  /**
   * @brief Build a human-readable description for a handler action.
   * @param handlerKey Handler key (e.g. "ngnt/open").
   * @return The tool description.
   */
  std::string          Describe(const std::string & handlerKey) const;
  /**
   * @brief Look up the declared MCP metadata for a handler action.
   * @param handlerKey Handler key (e.g. "ngnt/open").
   * @return Pointer to the tool info, or nullptr when none is registered.
   */
  const NMcpToolInfo * LookupToolInfo(const std::string & handlerKey) const;

  NHttpServer * fServer{nullptr}; ///< Server whose handlers are exposed
  Options         fOpts;            ///< MCP server options
  NRequestIdentity fCallerIdentity; ///< Caller tool calls run as (empty = anonymous)
};

} // namespace Ndmspc
#endif
