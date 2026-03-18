#ifndef Ndmspc_NGnRouteContext_H
#define Ndmspc_NGnRouteContext_H

#include <string>
#include <map>
#include <vector>
#include "NUtils.h"
#include "NLogger.h"

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
  NGnRouteContext(const std::string & method, json & in, json & out, json & wsOut,
                  std::map<std::string, TObject *> & objects);

  // --- Method checks ---
  bool IsGet() const { return fMethod.find("GET") != std::string::npos; }
  bool IsPost() const { return fMethod.find("POST") != std::string::npos; }
  bool IsPatch() const { return fMethod.find("PATCH") != std::string::npos; }
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
  NGnHttpServer * Server();

  // --- Parameter extraction from input JSON with defaults ---
  std::string GetString(const std::string & key, const std::string & def = "") const;
  int         GetInt(const std::string & key, int def = -1) const;
  double      GetDouble(const std::string & key, double def = 0.0) const;

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
  std::vector<int> GetStatePoint(const std::string & key = "spectra") const;
  void             SetStatePoint(const std::vector<int> & point, const std::string & key = "spectra");

  // --- Response helpers ---
  void Success();
  void Error(const std::string & msg);
  void Result(const std::string & status);

  // --- Workspace / state shortcuts ---
  json & Workspace();
  json & State();

  /// Copy a workspace section into wsOut for broadcasting.
  void BroadcastWorkspace(const std::string & name);

  // --- Raw access ---
  const std::string &                  Method() const { return fMethod; }
  json &                               In() { return fIn; }
  json &                               Out() { return fOut; }
  json &                               WsOut() { return fWsOut; }
  std::map<std::string, TObject *> &   Objects() { return fObjects; }
  bool                                 HasError() const { return fHasError; }

private:
  std::string                        fMethod;
  json &                             fIn;
  json &                             fOut;
  json &                             fWsOut;
  std::map<std::string, TObject *> & fObjects;
  bool                               fHasError{false};
};

} // namespace Ndmspc
#endif
