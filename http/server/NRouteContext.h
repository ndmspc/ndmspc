#ifndef Ndmspc_NGnRouteContext_H
#define Ndmspc_NGnRouteContext_H

#include <string>
#include <map>
#include <vector>
#include "ndmspc/core/NUtils.h"
#include "ndmspc/core/NLogger.h"

class TObject;

namespace Ndmspc {

class NHttpServer;

///
/// \class NRouteContext
/// \brief Lightweight context wrapping HTTP handler parameters.
///
/// Provides typed convenience methods to reduce boilerplate in handler
/// implementations: method checks, object access, parameter extraction
/// with workspace-default fallbacks, response helpers, and state management.
///
class NRouteContext {

public:
  /**
   * @brief Constructor.
   * @param method HTTP method of the request (e.g. "GET", "POST").
   * @param in Input JSON payload (referenced, not copied).
   * @param out Output JSON payload (referenced, not copied).
   * @param wsOut WebSocket output JSON payload (referenced, not copied).
   * @param objects Server object map (referenced, not copied).
   */
  NRouteContext(const std::string & method, json & in, json & out, json & wsOut,
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

  /**
   * @brief An object's name in this request's session: `<name>@<session>`, or `name` when the request
   *        belongs to no session (it names no combination).
   *
   * The input objects a tool creates belong to the session they were made in, so two sessions of the
   * same tool - two browser files, two analyses - keep their own. Tools use this for every
   * `AddInputObject`/`GetObject`/`RemoveInputObject` rather than the bare name.
   */
  std::string ObjectName(const std::string & name) const;

  // --- Access server singleton ---
  /// @brief Get the global NHttpServer instance.
  /// @return Pointer to the server (may be null).
  NHttpServer * Server();

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

  // --- Showing something in a pad ---

  /**
   * @brief Show one object in a pad: the generic envelope the viewport reads.
   *
   * Appends to `payload.pad`. The UI reads that as "fill a pad": `pad` names the pad (`pad1`,
   * `pad2`, …), `kind` names the renderer to draw it with (`jsroot`, `markdown`, `log`, `json`, …),
   * `value` is what that renderer takes, `options` its options, `label` the tab to show it on, and
   * `handlers` what a click on it means (see Action()). One frame may carry several — call it as
   * often as needed — and the pads, their grid and their tabs are the viewer's own.
   *
   * @param value What to draw.
   * @param kind Renderer id (default "json").
   * @param pad Pad to show it in (default "pad1").
   * @param label Tab to show it on (default: whatever the kind is called).
   * @param options Renderer options, e.g. `{{"drawOpts", "colz"}}`.
   * @param handlers Click/hover actions, e.g. `{{"click", json::array({Action("ndmspc/ngnt/map")})}}`.
   * @param replace Whether the pad should drop what it was showing first (this object replaces the
   *        pad's previous one).
   */
  void Show(const json & value, const std::string & kind = "json", const std::string & pad = "pad1",
            const std::string & label = "", const json & options = json::object(),
            const json & handlers = json::object(), bool replace = false);

  /**
   * @brief Show a ROOT object in a pad, serialized the way the `jsroot` renderer wants it.
   * @param object Object to draw (an error response is set when it is null).
   * @param pad Pad to show it in (default "pad1").
   * @param label Tab to show it on (default: the object's name).
   * @param drawOptions jsroot draw options ("colz", "lego", …).
   * @param handlers Click/hover actions.
   * @param replace Whether the pad should drop what it was showing first — the object replaces the
   *        pad's previous one rather than joining it (a new drawing over the last).
   */
  void ShowRoot(TObject * object, const std::string & pad = "pad1", const std::string & label = "",
                const std::string & drawOptions = "", const json & handlers = json::object(),
                bool replace = false);

  /**
   * @brief Show a modal dialog whose form is `schema` and whose submit carries out `action`.
   *
   * The UI renders `schema` with the same schema-driven form it uses for a tool's input and, on
   * submit, dispatches `action` (build it with Action()) with the form's values merged into its
   * payload. It is how a tool opens a small, purpose-built form over the view - a THnSparse's axis
   * picker, say - without adding a step to the combination tree. The dialog names no pad; the form
   * is the message, so a handler that opens one usually fills nothing else.
   *
   * @param title Dialog heading.
   * @param schema JSON Schema of the form (`{"type":"object","properties":{...}}`).
   * @param action The request the form's submit makes (see Action()).
   * @param options Extra dialog options, e.g. `{{"submit", "Project"}}`.
   */
  void Dialog(const std::string & title, const json & schema, const json & action,
              const json & options = json::object());

  /**
   * @brief An action a click can carry out, shaped the way the UI expects one.
   *
   * The UI adds the click's own `args` (the clicked bin, the container) beside `payload` when it
   * carries the action out, so a handler only builds the part it knows.
   *
   * @param path Handler path (the tool name with `/` instead of `_`, e.g. `ndmspc/ngnt/map`).
   * @param method HTTP verb.
   * @param payload Request body.
   * @param contentType Request content type.
   */
  static json Action(const std::string & path, const std::string & method = "PATCH",
                     const json & payload = json::object(),
                     const std::string & contentType = "application/json");

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
