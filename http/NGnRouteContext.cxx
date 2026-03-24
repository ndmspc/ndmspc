#include "NGnRouteContext.h"
#include "NGnHttpServer.h"
#include <cstdarg>
#include <vector>
#include <cstdio>

namespace Ndmspc {

NGnRouteContext::NGnRouteContext(const std::string & method, json & in, json & out, json & wsOut,
                                std::map<std::string, TObject *> & objects)
    : fMethod(method), fIn(in), fOut(out), fWsOut(wsOut), fObjects(objects)
{
}

NGnHttpServer * NGnRouteContext::Server() { return gNGnHttpServer; }

std::string NGnRouteContext::GetString(const std::string & key, const std::string & def) const
{
  if (fIn.contains(key)) return fIn[key].get<std::string>();
  return def;
}

int NGnRouteContext::GetInt(const std::string & key, int def) const
{
  if (fIn.contains(key)) return fIn[key].get<int>();
  return def;
}

double NGnRouteContext::GetDouble(const std::string & key, double def) const
{
  if (fIn.contains(key)) return fIn[key].get<double>();
  return def;
}

// --- Workspace default access ---

json NGnRouteContext::GetWorkspaceDefault(const std::string & route, const std::string & prop) const
{
  auto * srv = gNGnHttpServer;
  if (!srv) return json();
  auto & ws = srv->GetWorkspace();
  if (ws.contains(route) && ws[route].contains("properties") && ws[route]["properties"].contains(prop) &&
      ws[route]["properties"][prop].contains("default")) {
    return ws[route]["properties"][prop]["default"];
  }
  return json();
}

// --- State management ---

std::vector<int> NGnRouteContext::GetStatePoint(const std::string & key) const
{
  auto * srv = gNGnHttpServer;
  if (!srv) return {};
  json & state = srv->GetState();
  if (state.contains(key) && state[key].contains("point")) {
    return state[key]["point"].get<std::vector<int>>();
  }
  return {};
}

void NGnRouteContext::SetStatePoint(const std::vector<int> & point, const std::string & key)
{
  auto * srv = gNGnHttpServer;
  if (!srv) return;
  srv->GetState()[key]["point"] = point;
  // Also send updated state to websocket output
  fWsOut["state"] = srv->GetState();
}

// --- Response helpers ---

void NGnRouteContext::Success() { fOut["result"] = "success"; }

void NGnRouteContext::Error(const std::string & msg)
{
  fOut["error"] = msg;
  fHasError      = true;
}

void NGnRouteContext::Result(const std::string & status)
{
  // Treat Result as an indication of an error status in the HTTP output
  fOut["error"] = status;
  fHasError      = true;
}

void NGnRouteContext::Result(const char *fmt, ...)
{
  if (!fmt) {
    Result(std::string());
    return;
  }

  // Format the message using a growing buffer
  va_list ap;
  va_start(ap, fmt);
  // attempt with a fixed buffer first
  std::vector<char> buf(1024);
  int needed = vsnprintf(buf.data(), buf.size(), fmt, ap);
  va_end(ap);

  if (needed < 0) {
    // formatting error
    Result(std::string(fmt));
    return;
  }

  if (static_cast<size_t>(needed) >= buf.size()) {
    // need larger buffer
    buf.resize(static_cast<size_t>(needed) + 1);
    va_start(ap, fmt);
    vsnprintf(buf.data(), buf.size(), fmt, ap);
    va_end(ap);
  }

  Result(std::string(buf.data()));
}

// --- Workspace / state shortcuts ---

json & NGnRouteContext::Workspace()
{
  auto * srv = gNGnHttpServer;
  return srv->GetWorkspace();
}

json & NGnRouteContext::State()
{
  auto * srv = gNGnHttpServer;
  return srv->GetState();
}

void NGnRouteContext::BroadcastWorkspace(const std::string & name)
{
  auto * srv = gNGnHttpServer;
  if (srv && srv->GetWorkspace().contains(name)) {
    fWsOut["workspace"][name] = srv->GetWorkspace()[name];
  }
}

} // namespace Ndmspc
