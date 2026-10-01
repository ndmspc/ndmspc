#include <cstdarg>
#include <exception>
#include <vector>
#include <cstdio>
#include <TBufferJSON.h>
#include <TObject.h>
#include <TString.h>
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NInstanceTree.h"
#include "NRouteContext.h"

namespace Ndmspc {

NRouteContext::NRouteContext(const std::string & method, json & in, json & out, json & wsOut,
                                std::map<std::string, TObject *> & objects)
    : fMethod(method), fIn(in), fOut(out), fWsOut(wsOut), fObjects(objects)
{
}

NHttpServer * NRouteContext::Server() { return gNHttpServer; }

std::string NRouteContext::GetString(const std::string & key, const std::string & def) const
{
  if (fIn.contains(key)) return fIn[key].get<std::string>();
  return def;
}

int NRouteContext::GetInt(const std::string & key, int def) const
{
  if (fIn.contains(key)) return fIn[key].get<int>();
  return def;
}

double NRouteContext::GetDouble(const std::string & key, double def) const
{
  if (fIn.contains(key)) return fIn[key].get<double>();
  return def;
}

// --- Workspace default access ---

json NRouteContext::GetWorkspaceDefault(const std::string & route, const std::string & prop) const
{
  auto * srv = gNHttpServer;
  if (!srv) return json();
  auto & ws = srv->GetWorkspace();
  if (ws.contains(route) && ws[route].contains("properties") && ws[route]["properties"].contains(prop) &&
      ws[route]["properties"][prop].contains("default")) {
    return ws[route]["properties"][prop]["default"];
  }
  return json();
}

// --- State management ---

std::vector<int> NRouteContext::GetStatePoint(const std::string & key) const
{
  auto * srv = gNHttpServer;
  if (!srv) return {};
  // The point belongs to the combination node the request runs for; fall back to the live state
  // (an action outside a combination tree, or a node that has none yet).
  const std::string node = srv->GetCurrentInstance();
  if (!node.empty()) {
    NInstanceTree tree(srv->GetCombinations());
    const json    state = tree.State(node);
    if (state.is_object() && state.contains("point")) return state["point"].get<std::vector<int>>();
  }
  json & state = srv->GetState();
  if (state.contains(key) && state[key].contains("point")) {
    return state[key]["point"].get<std::vector<int>>();
  }
  return {};
}

void NRouteContext::SetStatePoint(const std::vector<int> & point, const std::string & key)
{
  auto * srv = gNHttpServer;
  if (!srv) return;
  // Store the point on the combination node it belongs to, so it comes back when that combination
  // is materialized again.
  const std::string node = srv->GetCurrentInstance();
  if (!node.empty()) {
    NInstanceTree tree(srv->GetCombinations());
    tree.SetState(node, json{{"point", point}});
  }
  // Mirror into the live state so the inspector metadata and the room snapshot keep working.
  srv->GetState()[key]["point"] = point;
  // Also send updated state to websocket output
  fWsOut["state"] = srv->GetState();
}

// --- Response helpers ---

void NRouteContext::Success() { fOut["result"] = "success"; }

void NRouteContext::Error(const std::string & msg)
{
  fOut["error"] = msg;
  fHasError      = true;
}

void NRouteContext::Result(const std::string & status)
{
  // Treat Result as an indication of an error status in the HTTP output
  fOut["error"] = status;
  fHasError      = true;
}

void NRouteContext::Result(const char *fmt, ...)
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

json & NRouteContext::Workspace()
{
  auto * srv = gNHttpServer;
  return srv->GetWorkspace();
}

json & NRouteContext::State()
{
  auto * srv = gNHttpServer;
  return srv->GetState();
}

void NRouteContext::BroadcastWorkspace(const std::string & name)
{
  auto * srv = gNHttpServer;
  if (srv && srv->GetWorkspace().contains(name)) {
    fWsOut["workspace"][name] = srv->GetWorkspace()[name];
  }
}

// --- Showing something in a pad ---

json NRouteContext::Action(const std::string & path, const std::string & method, const json & payload,
                           const std::string & contentType)
{
  json action;
  action["type"]        = "http";
  action["method"]      = method;
  action["path"]        = path;
  action["contentType"] = contentType;
  action["payload"]     = payload;
  return action;
}

void NRouteContext::Show(const json & value, const std::string & kind, const std::string & pad,
                         const std::string & label, const json & options, const json & handlers)
{
  json envelope;
  envelope["pad"]   = pad;
  envelope["kind"]  = kind;
  envelope["value"] = value;
  if (!label.empty()) envelope["label"] = label;
  if (!options.empty()) envelope["options"] = options;
  if (!handlers.empty()) envelope["handlers"] = handlers;

  // A frame may carry several objects, so the slot is a list. An envelope a handler wrote by hand
  // is kept as the first of them.
  json & slot = fWsOut["payload"]["pad"];
  if (slot.is_object() && !slot.empty()) {
    slot = json::array({slot});
  }
  else if (!slot.is_array()) {
    slot = json::array();
  }
  slot.push_back(std::move(envelope));
}

void NRouteContext::ShowRoot(TObject * object, const std::string & pad, const std::string & label,
                             const std::string & drawOptions, const json & handlers)
{
  if (object == nullptr) {
    Error("ShowRoot: no object to show");
    return;
  }

  const TString text = TBufferJSON::ConvertToJSON(object);
  if (text.IsNull() || text.Length() == 0) {
    Error(std::string("ShowRoot: could not serialize ") + object->ClassName());
    return;
  }

  json value;
  try {
    value = json::parse(text.Data());
  } catch (const std::exception & e) {
    Error(std::string("ShowRoot: ") + e.what());
    return;
  }

  json options = json::object();
  if (!drawOptions.empty()) options["drawOpts"] = drawOptions;

  // The object's own name reads well as a tab name.
  Show(value, "jsroot", pad, label.empty() ? std::string(object->GetName()) : label, options,
       handlers);
}

} // namespace Ndmspc
