/**
 * A harness for testing a tool macro the way the server runs it.
 *
 * A tool is a ROOT macro whose entry function calls `Ndmspc::RegisterMcpTool(...)`; `ndmspc-server`
 * opens it (`NUtils::OpenMacro`) and then routes requests to the handlers it registered. This does
 * the same — open the macro, then call a handler directly — and hands back the **websocket output**
 * the handler produced, which is what a viewer draws: the `payload.pad` envelopes, plus whatever
 * workspace the tool published.
 *
 * Nothing here knows a particular tool, so a new one is tested by loading its macro and calling its
 * handlers: the same calls a room would make, with nothing in between.
 *
 * The header (not a `.cxx`) is deliberate: `test/CMakeLists.txt` globs `*.cxx` and builds each as its
 * own test executable.
 */
#ifndef Ndmspc_NToolHarness_H
#define Ndmspc_NToolHarness_H

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <TMacro.h>
#include <TObject.h>

#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NHttpServer.h"

namespace Ndmspc::Test {

/// What one handler call produced.
struct ToolCall {
  /** The websocket output: `payload.pad` envelopes, `workspace`, `state` — what a viewer is sent. */
  json ws = json::object();
  /** The handler's own output: `result`, `error`, and anything it reports back to the caller. */
  json reply = json::object();
};

/**
 * One tool under test: its macro, the handlers the macro registered, and the objects those handlers
 * share between calls (`ngnt`, `navigator`, … — a chain builds them as it goes).
 */
class ToolHarness {
  public:
  /** Open a tool macro and take up its handlers, exactly as `ndmspc-server -m <macro>` would. */
  explicit ToolHarness(const std::string & macro)
  {
    // A tool registers into two registries that the *host* owns — the handler map and the tool
    // metadata. `ndmspc-server` creates them before loading a macro (the other tests do the same), so
    // a macro that registers without them dereferences a null map.
    gNdmspcHttpHandlers = &fHandlers;
    gNdmspcMcpTools     = &fToolInfo;

    fServer = std::make_unique<NHttpServer>("", /*ws=*/true, 10000, NOidcConfig{},
                                            /*startEngine=*/false);

    TMacro * loaded = NUtils::OpenMacro(macro);
    if (loaded == nullptr) {
      throw std::runtime_error("ToolHarness: could not open the macro " + macro);
    }
    loaded->Exec(); // the entry function runs, registering the tool's handlers
    delete loaded;

    fServer->SetHttpHandlers(fHandlers);
    fMacro = macro;
  }

  /** Hands the registries back, so one test's tool cannot leak into the next. */
  ~ToolHarness()
  {
    gNdmspcHttpHandlers = nullptr;
    gNdmspcMcpTools     = nullptr;
  }

  ToolHarness(const ToolHarness &)            = delete;
  ToolHarness & operator=(const ToolHarness &) = delete;

  /** The macro this harness loaded. */
  const std::string & Macro() const { return fMacro; }

  /**
   * Call one handler, the way the server calls it.
   * @param key The handler key (`ngnt/map`).
   * @param method The verb to act on (`GET`, `POST`, `PATCH`, `DELETE`).
   * @param payload The request body.
   * @return What the call produced: its websocket output and its reply.
   */
  ToolCall Call(const std::string & key, const std::string & method,
                const json & payload = json::object())
  {
    NHttpFuncPtr handler = fServer->FindHttpHandler(key);
    if (handler == nullptr) {
      throw std::runtime_error("ToolHarness: no handler for " + key);
    }

    json in    = payload;
    json out   = json::object();
    json wsOut = json::object();
    // The same map the server hands a handler, so an object one call registers
    // (`AddInputObject`) is the one the next call reads — as it is in production.
    handler(method, in, out, wsOut, fServer->GetObjectsMap());
    return fLast = ToolCall{wsOut, out};
  }

  /** The objects the tool has built so far, by the name its handlers use for them. */
  std::map<std::string, TObject *> & Objects() { return fServer->GetObjectsMap(); }

  /** The handler a tool registered under that key, or null when it registered none. */
  NHttpFuncPtr Handler(const std::string & key) { return fServer->FindHttpHandler(key); }

  /** What a tool declared about one of its actions (dependencies, order, label, schema), or null. */
  const NMcpToolInfo * ToolInfo(const std::string & key) const
  {
    if (gNdmspcMcpTools == nullptr) return nullptr;
    const auto it = gNdmspcMcpTools->find(key);
    return it == gNdmspcMcpTools->end() ? nullptr : &it->second;
  }

  /** The envelopes of a call, as a list — what the pads would be told to draw, in order. */
  static json Pads(const ToolCall & call)
  {
    if (!call.ws.contains("payload") || !call.ws["payload"].is_object() ||
        !call.ws["payload"].contains("pad")) {
      return json::array();
    }
    const json & pad = call.ws["payload"]["pad"];
    return pad.is_array() ? pad : json::array({pad});
  }

  /** The envelopes of the last call. */
  json Pads() const { return Pads(fLast); }

  /** The last call. */
  const ToolCall & Last() const { return fLast; }

  private:
  std::unique_ptr<NHttpServer> fServer;
  ToolCall                     fLast;
  /** The registries the loaded tool registers into, owned here while it is loaded. */
  NHttpHandlerMap                                            fHandlers;
  std::remove_pointer_t<decltype(gNdmspcMcpTools)>           fToolInfo;
  std::string                                               fMacro;
};

} // namespace Ndmspc::Test

#endif
