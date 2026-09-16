#ifndef Ndmspc_NGnRouteContext_H
#define Ndmspc_NGnRouteContext_H

#include <string>
#include <map>
#include <vector>
#include "ndmspc/core/NUtils.h"
#include "ndmspc/core/NLogger.h"

class TObject;

namespace Ndmspc {

class NGnHttpServer;

///
/// \class NGnRouteContext
/// \brief Lightweight context wrapping HTTP handler parameters.
///
/// Provides typed convenience methods to reduce boilerplate in handler
/// implementations: method checks, object access, parameter extraction
/// with workspace-default fallbacks, response helpers, and state management.
///
class NGnRouteContext {

public:
  /**
   * @brief Constructor.
   * @param method HTTP method of the request (e.g. "GET", "POST").
   * @param in Input JSON payload (referenced, not copied).
   * @param out Output JSON payload (referenced, not copied).
   * @param wsOut WebSocket output JSON payload (referenced, not copied).
   * @param objects Server object map (referenced, not copied).
   */
  NGnRouteContext(const std::string & method, json & in, json & out, json & wsOut,
                  std::map<std::string, TObject *> & objects);

  // --- Method checks ---
  /// @brief Whether the request method contains "GET".
  bool IsGet() const { return fMethod.find("GET") != std::string::npos; }
  /// @brief Whether the request method contains "POST".
  bool IsPost() const { return fMethod.find("POST") != std::string::npos; }
  /// @brief Whether the request method contains "PATCH".
  bool IsPatch() const { return fMethod.find("PATCH") != std::string::npos; }
  /// @brief Whether the request method contains "DELETE".
  bool IsDelete() const { return fMethod.find("DELETE") != std::string::npos; }

  // --- Typed object access ---

  /// Get a typed pointer from the server's object map. Returns nullptr if missing or wrong type.
  template <typename T>
  T * GetObject(const std::string & name)
  {
    auto it = fObjects.find(name);
    if (it != fObjects.end() && it->second) return dynamic_cast<T *>(it->second);
    return nullptr;
  }

  /// Get a typed pointer; sets an error response and returns nullptr if missing.
  template <typename T>
  T * RequireObject(const std::string & name)
  {
    T * obj = GetObject<T>(name);
    if (!obj) {
      Error(name + " is not available");
    }
    return obj;
  }

  // --- Access server singleton ---
  /// @brief Get the global NGnHttpServer instance.
  /// @return Pointer to the server (may be null).
  NGnHttpServer * Server();

  // --- Parameter extraction from input JSON with defaults ---
  /**
   * @brief Get a string parameter from the input JSON.
   * @param key Parameter key.
   * @param def Value returned when the key is missing (default: "").
   * @return The parameter value or def.
   */
  std::string GetString(const std::string & key, const std::string & def = "") const;
  /**
   * @brief Get an integer parameter from the input JSON.
   * @param key Parameter key.
   * @param def Value returned when the key is missing (default: -1).
   * @return The parameter value or def.
   */
  int         GetInt(const std::string & key, int def = -1) const;
  /**
   * @brief Get a double parameter from the input JSON.
   * @param key Parameter key.
   * @param def Value returned when the key is missing (default: 0.0).
   * @return The parameter value or def.
   */
  double      GetDouble(const std::string & key, double def = 0.0) const;

  /**
   * @brief Get a typed parameter from the input JSON.
   * @tparam T Parameter type.
   * @param key Parameter key.
   * @param def Value returned when the key is missing.
   * @return The parameter value or def.
   */
  template <typename T>
  T GetParam(const std::string & key, const T & def) const
  {
    if (fIn.contains(key)) return fIn[key].get<T>();
    return def;
  }

  /// Get a workspace property default value, or json() if not found.
  /// Path: workspace[route]["properties"][prop]["default"]
  json GetWorkspaceDefault(const std::string & route, const std::string & prop) const;

  // --- State management ---
  /**
   * @brief Get the state point stored under a key.
   * @param key State key (default: "spectra").
   * @return The stored point.
   */
  std::vector<int> GetStatePoint(const std::string & key = "spectra") const;
  /**
   * @brief Store a state point under a key.
   * @param point Point to store.
   * @param key State key (default: "spectra").
   */
  void             SetStatePoint(const std::vector<int> & point, const std::string & key = "spectra");

  // --- Response helpers ---
  /// @brief Mark the response as successful.
  void Success();
  /**
   * @brief Mark the response as failed and set an error message.
   * @param msg Error message.
   */
  void Error(const std::string & msg);
  /**
   * @brief Set the response result status.
   * @param status Result status string.
   */
  void Result(const std::string & status);
  /// printf-style result formatting: Result("format %s", arg)
  void Result(const char *fmt, ...);

  // --- Workspace / state shortcuts ---
  /// @brief Get the mutable workspace JSON from the server.
  json & Workspace();
  /// @brief Get the mutable state JSON from the server.
  json & State();

  /// Copy a workspace section into wsOut for broadcasting.
  void BroadcastWorkspace(const std::string & name);

  // --- Raw access ---
  /// @brief Get the request method.
  const std::string &                  Method() const { return fMethod; }
  /// @brief Get the mutable input JSON payload.
  json &                               In() { return fIn; }
  /// @brief Get the mutable output JSON payload.
  json &                               Out() { return fOut; }
  /// @brief Get the mutable WebSocket output JSON payload.
  json &                               WsOut() { return fWsOut; }
  /// @brief Get the server object map.
  std::map<std::string, TObject *> &   Objects() { return fObjects; }
  /// @brief Whether an error response has been set.
  bool                                 HasError() const { return fHasError; }

private:
  std::string                        fMethod;         ///< HTTP method of the request
  json &                             fIn;             ///< Referenced input JSON payload
  json &                             fOut;            ///< Referenced output JSON payload
  json &                             fWsOut;          ///< Referenced WebSocket output JSON payload
  std::map<std::string, TObject *> & fObjects;        ///< Referenced server object map
  bool                               fHasError{false}; ///< Whether an error response has been set
};

} // namespace Ndmspc
#endif
