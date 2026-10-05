#include <TROOT.h>
#include <TSystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#include <THttpCallArg.h>
#include <THttpServer.h>

#include "ndmspc/core/NCancellation.h"
#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NHistoryEntry.h"
#include "ndmspc/http/NHttpRequest.h"
#include "ndmspc/http/NInstanceTree.h"
#include "ndmspc/http/NMcpServer.h"
#include "ndmspc/http/NOidcHttpAuthenticator.h"
#include "ndmspc/http/NRoomAccess.h"
#include "ndmspc/http/NRoomSession.h"
#include "ndmspc/ndmspc.h"
#include "NActionWorker.h"
#include "NHttpServer.h"

// The process environment, as the C library exposes it. Declared here rather than pulling in
// <unistd.h> for the one symbol.
extern char **environ;

/// \cond CLASSIMP
ClassImp(Ndmspc::NHttpServer);
/// \endcond

namespace Ndmspc {

NHttpHandlerMap *    gNdmspcHttpHandlers   = nullptr;
NMcpToolMap *          gNdmspcMcpTools       = nullptr;
NHttpServer *          gNHttpServer        = nullptr;
NdmspcWsConnectFilter  gNdmspcWsConnectFilter = nullptr;

NHttpServer::NHttpServer(const char * engine, bool ws, int heartbeat_ms, NOidcConfig oidcConfig, bool startEngine)
    : THttpServer(startEngine ? engine : ""), fWsEnabled(ws), fHeartbeatMs(heartbeat_ms), fHeartbeatThread(nullptr)
{
  const auto authenticationTimeout = oidcConfig.authenticationTimeout;
  fAuthenticationTimeout = authenticationTimeout;

  // Build the shared OIDC token verifier once. The same verifier guards both
  // the WebSocket connections and the plain HTTP /api requests. When no OIDC
  // issuer/audience is configured the server stays in anonymous mode.
  if (oidcConfig.Enabled()) {
    auto authenticator = std::make_shared<NKeycloakOidcAuthenticator>(std::move(oidcConfig));
    authenticator->Initialize();
    fOidcVerifier = std::move(authenticator);
  }

  if (startEngine) {
    // THttpServer(engine) above already created the engine; finish the
    // WebSocket setup and heartbeat now.
    SetupWebSocketAndHeartbeat();
  }
  Ndmspc::gNHttpServer = this;
  fWorkspace.SetServer(this);

  // Inside a room the router says which room this is and where to report its session, so
  // that a room which scales to zero can be brought back as it was left.
  if (const char * roomId = std::getenv("NDMSPC_ROOM"); roomId != nullptr && *roomId != '\0') {
    fRoomId = roomId;
  }
  if (const char * stateUrl = std::getenv("NDMSPC_ROOM_STATE_URL"); stateUrl != nullptr && *stateUrl != '\0') {
    fRoomStateUrl = stateUrl;
  }
  while (!fRoomStateUrl.empty() && fRoomStateUrl.back() == '/') fRoomStateUrl.pop_back();

  if (!fRoomId.empty() && !fRoomStateUrl.empty()) {
    NLogInfo("Room '%s' reports its session to %s", fRoomId.c_str(), fRoomStateUrl.c_str());
  }

  // The tokens the router minted for this room: what lets it refuse a stranger. A room served
  // without them (an older image, or one created before access existed) enforces nothing.
  if (const char * access = std::getenv(NRoomAccess::kEnv); access != nullptr && *access != '\0') {
    fRoomAccess = NRoomAccess::Parse(access);
    if (!fRoomAccess.empty()) {
      NLogInfo("Room '%s' requires an access token (a read-write and a read-only one were minted)",
               fRoomId.c_str());
    }
  }
}

json NHttpServer::RoomAccessTokens() const
{
  std::lock_guard<std::mutex> lock(fRoomMutex);
  return fRoomAccess;
}

bool NHttpServer::RoomAccessRequired() const
{
  std::lock_guard<std::mutex> lock(fRoomMutex);
  // An object with tokens in it, or nothing: anything else (a server that was never a room, a
  // stray value) must not switch enforcement on.
  return fRoomAccess.is_object() && !fRoomAccess.empty();
}

std::string NHttpServer::RoomAccessLevel(const std::string & token) const
{
  std::lock_guard<std::mutex> lock(fRoomMutex);
  return NRoomAccess::LevelOf(fRoomAccess, token);
}

std::string NHttpServer::RoomAccessToken() const
{
  std::lock_guard<std::mutex> lock(fRoomMutex);
  if (!fRoomAccess.is_object()) return {};
  return fRoomAccess.value(NRoomAccess::kReadWrite, std::string());
}

std::string NHttpServer::RoomStateUrl(const std::string & base, const std::string & token)
{
  std::string url = base + "/api/room/state";
  if (!token.empty()) url += "?" + std::string(NRoomAccess::kParam) + "=" + token;
  return url;
}

std::string NHttpServer::RequestHeader(THttpCallArg * arg, const std::string & name)
{
  if (arg == nullptr || name.empty()) return {};

  const Int_t count = arg->NumRequestHeader();
  for (Int_t i = 0; i < count; ++i) {
    // CountHeader() hands back the name up to the ':', so a name written "X :" keeps its space.
    std::string header = arg->GetRequestHeaderName(i).Data();
    const auto  last   = header.find_last_not_of(" \t");
    header.erase(last == std::string::npos ? 0 : last + 1);
    if (header.size() != name.size()) continue;

    bool same = true;
    for (std::size_t c = 0; c < header.size() && same; ++c) {
      same = std::tolower(static_cast<unsigned char>(header[c])) ==
             std::tolower(static_cast<unsigned char>(name[c]));
    }
    if (!same) continue;

    const TString value = arg->GetRequestHeader(header.c_str());
    return value.IsNull() ? std::string() : std::string(value.Data());
  }
  return {};
}

json NHttpServer::RuntimeEnv()
{
  json env = json::object();

  // Only the VITE_ prefix: Vite hands exactly these to the client, so a runtime setting cannot
  // disclose more than the build already does. Everything else the process was started with
  // (NDMSPC_SLURM_*, NDMSPC_ROOM_*, ...) is internal and stays out.
  for (char ** item = environ; item != nullptr && *item != nullptr; ++item) {
    const std::string entry(*item);
    const auto        equals = entry.find('=');
    if (equals == std::string::npos) continue;
    const std::string name = entry.substr(0, equals);
    if (name.rfind("VITE_", 0) != 0) continue;
    env[name] = entry.substr(equals + 1);
  }
  return env;
}

std::string NHttpServer::JsonForHtml(const json & value)
{
  std::string text = value.dump();

  // Each character is replaced by JSON's own \uXXXX escape, which is legal inside a JSON string:
  // the text stays the same JSON value while it can no longer close the <script> element.
  static const std::pair<const char *, const char *> escapes[] = {
      {"<", "\\u003c"}, {">", "\\u003e"}, {"&", "\\u0026"}, {"\xE2\x80\xA8", "\\u2028"}, {"\xE2\x80\xA9", "\\u2029"}};

  for (const auto & escape : escapes) {
    const std::size_t from = std::strlen(escape.first);
    const std::size_t to   = std::strlen(escape.second);
    for (std::size_t pos = 0; (pos = text.find(escape.first, pos)) != std::string::npos; pos += to)
      text.replace(pos, from, escape.second);
  }
  return text;
}

std::string NHttpServer::InjectRuntimeEnv(const std::string & html, const json & env)
{
  if (env.empty() || html.empty()) return {};

  const std::string script = "<script>window.__NDMSPC_ENV__ = " + JsonForHtml(env) + ";</script>\n";

  // Ahead of the page's own scripts, so a module that reads a setting while it loads already sees
  // it. The app's scripts live in <head>; a page without one gets the script at the front, which
  // the parser still runs before anything later.
  const std::string open = "<head>";
  const auto        head = html.find(open);
  if (head != std::string::npos) return html.substr(0, head + open.size()) + "\n" + script + html.substr(head + open.size());

  const std::string close = "</head>";
  const auto        end   = html.find(close);
  if (end != std::string::npos) return html.substr(0, end) + script + html.substr(end);

  return script + html;
}

void NHttpServer::PrepareRuntimeEnvPage()
{
  fRuntimeEnvPage.clear();

  const json env = RuntimeEnv();
  if (env.empty()) return; // Nothing to hand over: the page is served as it was built.
  if (fDefaultPage.empty()) return;

  const std::string html = ReadFileContent(fDefaultPage);
  if (html.empty()) {
    NLogWarning("Cannot read the default page '%s' to inject the VITE_* settings", fDefaultPage.c_str());
    return;
  }

  // A page that asks THttpServer to finish it (the JSROOT pages carry these placeholders) is left
  // to THttpServer: answering it here would leave them unresolved.
  if (html.find("<!--jsroot_importmap-->") != std::string::npos || html.find("$$$h.json$$$") != std::string::npos) {
    NLogWarning("The default page '%s' is served without the VITE_* settings: it needs THttpServer's own substitutions",
                fDefaultPage.c_str());
    return;
  }

  fRuntimeEnvPage = InjectRuntimeEnv(html, env);
  if (!fRuntimeEnvPage.empty())
    NLogInfo("Serving the page with %zu VITE_* setting(s) from the environment", env.size());
}

std::string NHttpServer::RequestAccessToken(THttpCallArg * arg) const
{
  if (arg == nullptr) return {};

  // The link carries the token; a script may send it as a header; a browser is given a cookie by
  // the page it loaded, because a page's own scripts cannot add a header to their API calls.
  const char * query = arg->GetQuery();
  std::string  token = NRoomAccess::TokenFromQuery(query != nullptr ? query : "");
  if (!token.empty()) return token;

  token = RequestHeader(arg, NRoomAccess::kHeader);
  if (!token.empty()) return token;

  return NRoomAccess::TokenFromCookie(RequestHeader(arg, "Cookie"));
}

bool NHttpServer::ApplyRoomAccess(THttpCallArg * arg, const std::string & method, bool isPage,
                                 bool alreadyAdmitted)
{
  if (arg == nullptr || !RoomAccessRequired()) return true;

  // A request the server dispatched itself is not a new request from a client: a tool call runs
  // for a caller whose own request already carried the room's token (and whose WebSocket
  // connection was admitted with one), and a replay has no client at all. The gate above says the
  // same about the bearer check.
  if (alreadyAdmitted) return true;

  // A request bridged from a WebSocket carries no token of its own: that connection was admitted
  // with one at its upgrade, and the read-only rule is applied there (see NWsHandler).
  if (arg->GetWSId() != 0) return true;

  const std::string token = RequestAccessToken(arg);
  const std::string level = RoomAccessLevel(token);

  if (level.empty()) {
    NLogWarning("Refusing a %s request to %s of room '%s': %s", method.c_str(), isPage ? "the page" : "/api",
                fRoomId.c_str(), token.empty() ? "no access token" : "an unknown access token");
    if (isPage) {
      // ROOT's civetweb cannot send a body with an error status, so _404_ is the only refusal it
      // offers - which also avoids telling a stranger that this room exists at all.
      arg->Set404();
    }
    else {
      // The same shape the OIDC gate uses: HTTP 200, with the reason in the JSON envelope.
      arg->SetContentType("application/json");
      arg->SetContent(json{{"result", "failure"},
                           {"error", token.empty() ? "this room requires an access token"
                                                   : "this room does not accept that access token"},
                           {"code", token.empty() ? "access_denied" : "invalid_access_token"}}
                          .dump());
      arg->AddNoCacheHeader();
    }
    return false;
  }

  // A page link states the level it was handed out at (`?access=rw|ro`), so a viewer knows a
  // read-only link without asking for what the room would refuse. `access` is a hint, and the token
  // is what opens the room - so a link whose level does not agree with its token is not admitted, and
  // the value the page then acts on is one this room agreed with (editing the link changes nothing).
  // A link that states no level (one handed out before this existed) is admitted as it always was.
  if (isPage) {
    const char *      query  = arg->GetQuery();
    const std::string stated = NRoomAccess::LevelFromQuery(query != nullptr ? query : "");
    if (!stated.empty() && stated != level) {
      NLogWarning("Refusing the page of room '%s': the link states access '%s' but its token grants '%s'",
                  fRoomId.c_str(), stated.c_str(), level.c_str());
      arg->Set404();
      return false;
    }
  }

  if (level == NRoomAccess::kReadOnly && method.find("GET") == std::string::npos) {
    NLogWarning("Refusing a %s request to /api of room '%s': that token is read-only", method.c_str(),
                fRoomId.c_str());
    arg->SetContentType("application/json");
    arg->SetContent(json{{"result", "failure"},
                         {"error", "this room's access token is read-only"},
                         {"code", "read_only"}}
                        .dump());
    arg->AddNoCacheHeader();
    return false;
  }

  // A browser that arrived on a link hands the token on as a cookie, so the page's own scripts keep
  // working for the rest of the session.
  if (isPage) {
    const std::string cookie =
        std::string(NRoomAccess::kCookie) + "=" + token + "; Path=/; HttpOnly; SameSite=Lax";
    arg->AddHeader("Set-Cookie", cookie.c_str());
  }
  return true;
}

bool NHttpServer::StartEngine(const char * engine)
{
  // Idempotent: nothing to do when an engine is already running.
  if (fEngineStarted || IsAnyEngine()) {
    fEngineStarted = true;
    SetupWebSocketAndHeartbeat();
    return IsAnyEngine();
  }
  if (!engine || !*engine) return false;

  // The page has been set by now (SetDefaultPage runs before the engine is started), so this is
  // the last moment before anything can be served to put the deployment's VITE_* settings into it.
  PrepareRuntimeEnvPage();

  // Create the engine (starts civetweb listening). When the engine fails to
  // bind (e.g. port in use) no engine is added and IsAnyEngine() is false.
  if (!CreateEngine(engine)) return false;
  fEngineStarted = true;
  SetupWebSocketAndHeartbeat();
  return IsAnyEngine();
}

void NHttpServer::SetupWebSocketAndHeartbeat()
{
  if (fWsEnabled && !fNWsHandler) {
    fNWsHandler = new NWsHandler("ws", "ws", fOidcVerifier, fAuthenticationTimeout);
    Register("/", fNWsHandler);
  }
  if (fHeartbeatMs > 0 && fNWsHandler && !fHeartbeatThread) StartHeartbeatThread();
}

void NHttpServer::SetHeartbeatMs(int ms)
{
  std::lock_guard<std::mutex> lk(fHeartbeatMutex);
  fHeartbeatMs = ms;
  // restart thread according to new interval
  StopHeartbeatThread();
  if (fNWsHandler && fHeartbeatMs > 0) StartHeartbeatThread();
}

NHttpServer::~NHttpServer()
{
  StopHeartbeatThread();
  // Stop the action worker last: it may be running an action that touches this server's state, so it
  // is joined (finished) before the rest is torn down.
  {
    std::lock_guard<std::mutex> lock(fWorkerMutex);
    delete fActionWorker;
    fActionWorker = nullptr;
  }
}

void NHttpServer::StartHeartbeatThread()
{
  if (fHeartbeatThread || fHeartbeatMs <= 0) return;
  fHeartbeatRunning.store(true);
  fHeartbeatThread = new std::thread([this]() {
    std::unique_lock<std::mutex> lk(fHeartbeatCvMutex);
    while (fHeartbeatRunning.load()) {
      // wait_for returns when notified or when timeout elapses
      auto dur = std::chrono::milliseconds(fHeartbeatMs);
      // release fHeartbeatCvMutex while waiting but will reacquire on wake
      fHeartbeatCv.wait_for(lk, dur, [this]() { return !fHeartbeatRunning.load(); });
      if (!fHeartbeatRunning.load()) break;
      try {
        // Delegate to NWsHandler's timer handler so it updates snapshots (file/net) and broadcasts
        if (fNWsHandler) {
          fNWsHandler->HandleTimer(nullptr);
        } else {
          // fallback: simple heartbeat
          json data = json::object();
          data["event"] = "heartbeat";
          json payload = json::object();
          payload["count"] = ++fServCnt;
          payload["clients"] = 0;
          data["payload"] = payload;
          WebSocketBroadcast(data);
        }
      } catch (...) {
        // swallow errors to keep thread alive
      }
    }
  });
}

void NHttpServer::StopHeartbeatThread()
{
  if (!fHeartbeatThread) return;
  fHeartbeatRunning.store(false);
  // Wake up the sleeping heartbeat thread immediately
  fHeartbeatCv.notify_all();
  if (fHeartbeatThread->joinable()) fHeartbeatThread->join();
  delete fHeartbeatThread;
  fHeartbeatThread = nullptr;
}

bool NHttpServer::WebSocketBroadcast(json message)
{
  NLogTrace("Broadcasting message to all clients.");
  if (fNWsHandler) {
    std::string msgStr = message.dump();
    fNWsHandler->BroadcastUnsafe(msgStr);
    return true;
  }
  return false;
}

void NHttpServer::SetHttpHandlers(std::map<std::string, NHttpFuncPtr> handlers)
{
  std::lock_guard<std::mutex> lock(fHandlersMutex);
  fHttpHandlers = std::move(handlers);
}

std::map<std::string, NHttpFuncPtr> NHttpServer::GetHttpHandlers() const
{
  std::lock_guard<std::mutex> lock(fHandlersMutex);
  return fHttpHandlers;
}

NHttpFuncPtr NHttpServer::FindHttpHandler(const std::string & name) const
{
  std::lock_guard<std::mutex> lock(fHandlersMutex);
  const auto it = fHttpHandlers.find(name);
  return it != fHttpHandlers.end() ? it->second : nullptr;
}

void NHttpServer::Print(Option_t * option) const
{
  THttpServer::Print(option);
  // print HTTP handlers
  // NLogInfo("HTTP Handlers:");
  // for (const auto & handler : fHttpHandlers) {
  //   NLogInfo("  %s", handler.first.c_str());
  // }
  // print all input objects
  NLogInfo("Input Objects:");
  for (const auto & obj : fObjectsMap) {
    NLogInfo("  %s -> %p", obj.first.c_str(), obj.second);
  }
  // print history entries
  fWorkspace.Print(option);
}

// ---------------------------------------------------------------------------
//  Room session (see Ndmspc::NRoomRouter)
// ---------------------------------------------------------------------------

namespace {
/// @brief Wait this long before trying a failed session fetch again.
constexpr long kRoomRestoreRetrySeconds = 5;

/// @brief How long a room waits on the router for its session, and to report one.
///
/// Long enough for a cold router, short enough that a client is not left waiting: the fetch
/// happens before the waking request is served, and the router has a single concurrency, so
/// it can be busy for a while.
constexpr int  kRoomRouterConnectMs  = 3000;
constexpr int  kRoomRouterReadMs     = 5000;
/// @brief Fetch attempts before serving without the session, and the pause between them.
///
/// The router may be scaled to zero: the first attempt is then only answered once it has
/// started, so retrying within the request lets a cold router still restore the session while
/// the short timeouts keep the total wait bounded (a few tens of seconds at worst).
constexpr int  kRoomRestoreFetchAttempts = 3;
constexpr long kRoomRestoreFetchPauseMs  = 1000;

/// @brief Monotonic seconds, for retry timing.
long SteadySeconds()
{
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

/// @brief Read a member without throwing when it is absent.
json JsonMember(const json & object, const char * key)
{
  if (!object.is_object() || !object.contains(key)) return json();
  return object[key];
}
} // namespace

void NHttpServer::RecordPads(const std::string & group, const json & envelopes)
{
  // One entry per (pad, tab label): re-drawing a map, or another point of one, replaces the tabs it
  // already drew rather than stacking another copy, so the record is what the group is showing and not
  // its whole history. The order is the order they first appeared, so the tabs come back as they were.
  json & kept = fPads[group];
  if (!kept.is_array()) kept = json::array();

  // A pad and a tab are named by whatever the drawing said - a number or a string - so the key is read
  // without pinning a type: asking for a string where the client sent a number throws, and remembering a
  // drawing is no place to refuse one.
  const auto keyOf = [](const json & one) {
    return one.value("pad", json()).dump() + "\n" + one.value("label", json()).dump();
  };

  const json list = envelopes.is_array() ? envelopes : json::array({envelopes});
  for (const auto & envelope : list) {
    if (!envelope.is_object()) continue;
    const std::string key      = keyOf(envelope);
    bool              replaced = false;
    for (auto & entry : kept) {
      if (!entry.is_object()) continue;
      if (keyOf(entry) == key) {
        entry    = envelope;
        replaced = true;
        break;
      }
    }
    if (!replaced) kept.push_back(envelope);
  }
}

json NHttpServer::SessionState()
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  const json & workspace = fWorkspace.GetWorkspace();

  json schema;
  schema["properties"] = workspace.is_object() ? workspace : json::object();
  if (!fGroup.empty()) schema["group"] = fGroup;

  json payload;
  payload["combinations"]        = tree.ToTree();
  payload["workspace"]["schema"] = schema;
  // What each session has drawn: a client that has just joined is shown the room's pads, not an empty
  // view (it keeps them per session and shows the one it is looking at). A request that named no
  // session is filed under its group, so the key is a session id or a group name.
  payload["pads"]                = fPads;
  // The tool group the room is looking at, so a client that joins opens on the same one everyone else
  // is on rather than on its own last choice (see the `group` base action).
  payload["group"]               = fGroup;
  // Each group's own schemas, kept apart: the flat `workspace.schema` above collides (two groups both
  // publish "open"), so a client that read it would take one group's live default for the other's -
  // the browser opening on the file the analysis tool had open. This is what it reads instead.
  payload["workspaces"]          = fWorkspaceByGroup;
  // Each session's own schemas, so a client can key its defaults by session. The group-keyed map
  // above is kept for a client that still reads that.
  payload["sessionWorkspaces"]   = fWorkspaceBySession;
  // The room's sessions - one per open file - and which one is active, so a client can offer the
  // picker before the tree itself is read.
  const json sessions            = SessionList();
  payload["session"]             = sessions["session"];
  payload["sessions"]            = sessions["sessions"];

  return json{{"event", "ngnt"}, {"payload", payload}};
}

/// @brief The number in a node id ("i7" -> 7), or 0 when it is not one of ours.
static int NodeNumber(const std::string & id)
{
  if (id.size() < 2) return 0;
  try {
    return std::stoi(id.substr(1));
  } catch (...) {
    return 0;
  }
}

/**
 * @brief A session's own id: a short hash, so what identifies it never reads as a name.
 *
 * A session is identified by something opaque and a person reads it by the name the room holds (see
 * RenameSession): the two are kept apart on purpose, so renaming one is not renaming an id, and an
 * unnamed session does not look like it was named after its file. The group and the node it is make it
 * unique among a room's sessions.
 */
static std::string SessionToken(const std::string & group, int counter)
{
  const std::size_t hashed = std::hash<std::string>{}(group + "#" + std::to_string(counter));
  char              buf[16];
  std::snprintf(buf, sizeof(buf), "%012llx", static_cast<unsigned long long>(hashed) & 0xffffffffffffULL);
  return std::string(buf);
}

json NHttpServer::SessionList()
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  // One entry per root: a session is whatever a group started with - an `open`, or any other first
  // step - so this is the root list, not a list of files. What a session is *called* is the room's
  // (see RenameSession), which wins over what the node itself is called; the two are separate things.
  json sessions = json::array();
  for (const auto & root : tree.Roots()) {
    const auto named = fSessionNames.find(root);
    const std::string group = Ndmspc::NInstanceTree::GroupOf(tree.Action(root));
    json       one;
    one["id"]    = root;
    one["group"] = group;
    one["label"] = named != fSessionNames.end() ? named->second
                                                : tree.Get(root).value("label", std::string());
    // The id it is identified by, apart from the name it is called: a session started from the bar
    // carries it in its arguments, and one started by a plain step is identified the same way - by the
    // node it is. A rename dialog shows it, so it is clear which session is being named.
    one["hash"] = SessionToken(group, NodeNumber(root));
    sessions.push_back(std::move(one));
  }

  // Forget the names of sessions that are gone, so a room that opens and closes all day does not keep
  // a name for every one it ever had.
  for (auto it = fSessionNames.begin(); it != fSessionNames.end();) {
    if (tree.Has(it->first)) ++it;
    else it = fSessionNames.erase(it);
  }

  const auto active = tree.Active();
  json       out;
  out["session"]  = active.empty() ? std::string() : active.front();
  out["sessions"] = std::move(sessions);
  return out;
}

bool NHttpServer::ActivateSession(const std::string & session)
{
  if (session.empty()) return false;

  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());
  if (!tree.Has(session)) return false;

  // Its live chain becomes its group's live one, and the room's active session with it. Nothing is
  // materialized: a session's objects and defaults are kept for as long as it exists, so they are
  // still there when it comes back into view.
  tree.SetActive(Ndmspc::NInstanceTree::GroupOf(tree.Action(session)), tree.Path(session));
  return true;
}

bool NHttpServer::RenameSession(const std::string & session, const std::string & name)
{
  if (session.empty() || name.empty()) return false;

  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());
  if (!tree.Has(session)) return false;

  // The room's own name for it - beside its group and its view, not part of any tool's parameters and
  // not what the node itself is called. Its action and arguments, so its file, objects, defaults and
  // pads, are untouched, and the name survives a re-open.
  fSessionNames[session] = name;
  return true;
}

std::string NHttpServer::FreshSessionRoot(const std::string & action)
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  // The session the room is on, and only while it is this action's group's own.
  const std::vector<std::string> active = tree.Active();
  if (active.empty()) return "";
  const std::string root = active.front();
  if (root.empty() || !tree.Has(root)) return "";
  if (Ndmspc::NInstanceTree::GroupOf(tree.Action(root)) != Ndmspc::NInstanceTree::GroupOf(action)) return "";

  // Still marked as not run, so this run is its first step. Once it has run it is a session with a first
  // step, and a run is a step of it - or, for other arguments, a session of its own.
  const json params = tree.Params(root);
  if (params.is_object() && params.value("pending", false)) return root;
  return "";
}

std::string NHttpServer::SessionNameFor(const std::string & group, const std::string & exclude)
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  int                   count = 0;
  std::set<std::string> taken;
  for (const auto & root : tree.Roots()) {
    if (Ndmspc::NInstanceTree::GroupOf(tree.Action(root)) != group) continue;
    // The session being named is not one of the ones already there: its number is the next one.
    if (root == exclude) continue;
    ++count;
    const auto named = fSessionNames.find(root);
    if (named != fSessionNames.end()) taken.insert(named->second);
  }

  // The next number for the group, stepping over a name one of its sessions already has (a session that
  // was removed leaves a gap, and a name is not handed out twice).
  std::string name;
  do {
    name = group + " " + std::to_string(++count);
  } while (taken.count(name) != 0);
  return name;
}

std::string NHttpServer::GroupSession(const std::string & group)
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  // What the group's own live chain is on, else the group's first session: what a view of that group
  // should be showing.
  const std::vector<std::string> active = tree.Active(group);
  if (!active.empty() && tree.Has(active.front())) return active.front();
  for (const auto & root : tree.Roots()) {
    if (Ndmspc::NInstanceTree::GroupOf(tree.Action(root)) == group) return root;
  }
  return "";
}

std::string NHttpServer::StartSession(const std::string & group, const std::string & name)
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  // The group's own first action - what a session of it starts with - by declaration, not by name.
  std::string opener;
  int         openerOrder = 0;
  if (gNdmspcMcpTools != nullptr) {
    for (const auto & entry : *gNdmspcMcpTools) {
      const Ndmspc::NMcpToolInfo & info = entry.second;
      if (!info.session || Ndmspc::NInstanceTree::GroupOf(entry.first) != group) continue;
      if (opener.empty() || info.order < openerOrder) {
        opener      = entry.first;
        openerOrder = info.order;
      }
    }
  }
  if (opener.empty()) return "";

  // A session that has been started and not yet run is the one to be on: every client asks for one as
  // it joins, and they must all land on the same session rather than each making their own.
  for (const auto & root : tree.Roots()) {
    if (Ndmspc::NInstanceTree::GroupOf(tree.Action(root)) != group) continue;
    const json params = tree.Params(root);
    if (params.is_object() && params.value("pending", false)) {
      ActivateSession(root);
      return root;
    }
  }

  json & store = fWorkspace.GetCombinations();

  // An id of its own, so the root is a node no other session shares: the tree's own counter (which a
  // restored tree carries on from the snapshot) says which one it is, and the hash makes it read as an
  // id rather than a name.
  const std::string token = SessionToken(group, store.value("next", 1));

  // The arguments its first step starts with: the tool's own defaults, so a new session is not blank -
  // it is ready to run, and the user changes what they want. The token identifies it, and `pending`
  // says its first step has not run yet (see FreshSessionRoot).
  json params;
  params["session"] = token;
  params["pending"] = true;
  if (gNdmspcMcpTools != nullptr) {
    const auto info = gNdmspcMcpTools->find(opener);
    if (info != gNdmspcMcpTools->end() && info->second.inputSchema.is_object()) {
      const json & schema = info->second.inputSchema;
      const json   props  = schema.contains("properties") ? schema["properties"] : json::object();
      if (props.is_object()) {
        for (auto it = props.begin(); it != props.end(); ++it) {
          if (it.value().is_object() && it.value().contains("default")) {
            params[it.key()] = it.value()["default"];
          }
        }
      }
    }
  }

  // The step is called what its own tool says it is - `{{ file }}` for an `open`, so the file it opened -
  // and the room's name is the session's, not the node's: the picker reads "browser 1" while the step
  // reads what it is working on. A tool that opens no file declares its own label and gets the same
  // treatment.
  const std::string sessionName = name.empty() ? SessionNameFor(group) : name;
  const std::string id          = tree.Create(opener, params, "", "");
  fSessionNames[id]             = sessionName;
  ActivateSession(id);
  return id;
}

json NHttpServer::RoomSessionSnapshot()
{
  // A session is exactly what the ngnt/open and ngnt/reshape POSTs recorded plus the
  // drill-down point, and the workspace already tracks all of it.
  const json        openSchema = JsonMember(JsonMember(fWorkspace.GetWorkspace(), "open"), "properties");
  const std::string file       = NUtils::GetJsonString(JsonMember(JsonMember(openSchema, "file"), "default"));
  if (file.empty()) return json();

  // GetJson() returns the history array itself, while the server's root endpoint wraps the
  // same thing as {"state": {"history": [...]}}. Accept either shape: reading the wrong one
  // silently yields a snapshot with no replayable actions.
  json       history;
  const json root = GetJson();
  if (root.is_array()) {
    history = root;
  }
  else if (root.is_object() && root.contains("state")) {
    history = JsonMember(root["state"], "history");
  }

  const json & state = fWorkspace.GetState();
  json         point;
  if (state.is_object() && state.contains("spectra")) point = JsonMember(state["spectra"], "point");

  json snapshot = NRoomSession::Build(fRoomId, file, history, point);
  if (snapshot.is_null()) return snapshot;

  // The whole combination tree rides along, so a room that wakes can rebuild every combination it
  // held, not only the one that was live. The snapshot is keyed by tool group (v3); a snapshot without
  // it (an older room, or a session that declares no dependencies) restores the single combination
  // the `actions` describe.
  const json combinations = Ndmspc::NInstanceTree(fWorkspace.GetCombinations()).Snapshot();
  if (Ndmspc::NInstanceTree::HasNodes(combinations)) {
    snapshot["combinations"] = combinations;
  }

  // The room's own session state - the names it holds and what each session has drawn - rides beside the
  // tree: a room that wakes comes back with the names it was using, not ones derived again from whatever
  // each file happens to be, and with the pads it was showing.
  const json sessionState = SessionStateSnapshot();
  for (auto it = sessionState.begin(); it != sessionState.end(); ++it) {
    snapshot[it.key()] = it.value();
  }
  return snapshot;
}

json NHttpServer::SessionStateSnapshot() const
{
  json state;
  if (!fSessionNames.empty()) {
    json names = json::object();
    for (const auto & entry : fSessionNames) {
      names[entry.first] = entry.second;
    }
    state["sessionNames"] = names;
  }
  if (fPads.is_object() && !fPads.empty()) {
    state["pads"] = fPads;
  }
  return state;
}

void NHttpServer::AdoptSessionState(const json & state)
{
  const json names = JsonMember(state, "sessionNames");
  if (names.is_object()) {
    fSessionNames.clear();
    for (auto it = names.begin(); it != names.end(); ++it) {
      if (it.value().is_string()) fSessionNames[it.key()] = it.value().get<std::string>();
    }
  }
  const json pads = JsonMember(state, "pads");
  if (pads.is_object()) fPads = pads;
}

void NHttpServer::RoomSessionPush()
{
  if (fRoomId.empty() || fRoomStateUrl.empty()) return;

  const json snapshot = RoomSessionSnapshot();
  if (snapshot.is_null()) return; // nothing open: never report an empty session

  const std::string text = NRoomSession::Encode(snapshot);
  if (text.empty()) return;

  {
    std::lock_guard<std::mutex> lock(fRoomMutex);
    if (fRoomPushed == text) return; // unchanged since the last report
  }

  json message;
  message["room"]     = fRoomId;
  message["snapshot"] = snapshot;

  const std::string file  = snapshot.value("file", std::string());
  const std::string url   = RoomStateUrl(fRoomStateUrl, RoomAccessToken());
  const std::string body  = message.dump();
  const std::string room  = fRoomId;

  // Report from a worker thread. The router is a single-concurrency Knative service that can
  // be cold or busy, and the client's own request must not wait on this side channel - with
  // one request at a time (containerConcurrency 1) a slow report would also delay the next
  // client. A failed report leaves the last reported text in place, so the next change
  // retries; the server outlives the thread.
  std::thread([this, url, body, text, room, file]() {
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    NHttpRequest http;
    http.SetTimeout(kRoomRouterConnectMs, kRoomRouterReadMs);
    std::string reply;
    try {
      const NHttpResponse response = http.request("POST", url, body, headers);
      if (response.status < 200 || response.status >= 300) {
        NLogWarning("Cannot report the session of room '%s' (HTTP %d)", room.c_str(), response.status);
        return;
      }
      reply = response.body;
    }
    catch (const std::exception & e) {
      NLogWarning("Cannot report the session of room '%s': %s", room.c_str(), e.what());
      return;
    }

    // Whether the router took the report is in the body, not in the status: the embedded ROOT
    // server cannot attach a body to an error status, so a refusal arrives as HTTP 200 with a
    // JSON error body. Reading only the status would record a refused report as a success and
    // never report that session again.
    json parsed;
    try {
      parsed = json::parse(reply);
    }
    catch (const json::parse_error &) {
      NLogWarning("Cannot report the session of room '%s': the router answered something that is not JSON",
                  room.c_str());
      return;
    }
    if (!parsed.is_object() || NUtils::GetJsonString(JsonMember(parsed, "result")) != "success") {
      const std::string detail = NUtils::GetJsonString(JsonMember(parsed, "error"));
      NLogWarning("The router refused the session of room '%s': %s", room.c_str(),
                  detail.empty() ? "it did not report success" : detail.c_str());
      return; // fRoomPushed is left as it was, so the next change reports again
    }

    {
      std::lock_guard<std::mutex> lock(fRoomMutex);
      fRoomPushed = text;
    }
    NLogInfo("Reported the session of room '%s' (file '%s')", room.c_str(), file.c_str());
  }).detach();
}

void NHttpServer::RoomSessionRestoreOnce()
{
  if (fRoomId.empty() || fRoomStateUrl.empty()) return;

  {
    std::lock_guard<std::mutex> lock(fRoomMutex);
    if (fRoomRestoring) return; // the nested replay is already running
    if (fRoomRestored) return;
    // A failed fetch is retried on a later request instead of giving up for good: the router
    // has a single concurrency, so while it is busy with somebody's room/open (which can take
    // minutes) the room's own call times out until it frees up.
    if (SteadySeconds() < fRoomRestoreNextTrySec) return;
    fRoomRestoring = true;
  }

  const auto release = [this](bool restored) {
    std::lock_guard<std::mutex> lock(fRoomMutex);
    fRoomRestoring = false;
    if (restored) {
      fRoomRestored = true;
    }
    else {
      fRoomRestoreNextTrySec = SteadySeconds() + kRoomRestoreRetrySeconds;
    }
  };

  // The room id travels in the body: a query would be matched by this room's own HTTPRoute.
  // The read is a POST because NHttpRequest forwards the body for POST but not for GET, and
  // an id-less request would be rejected by the router.
  json request;
  request["room"] = fRoomId;

  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";

  const std::string url = RoomStateUrl(fRoomStateUrl, RoomAccessToken());

  NHttpResponse response;
  bool          fetched    = false;
  std::string   fetchError;
  for (int attempt = 1; attempt <= kRoomRestoreFetchAttempts && !fetched; ++attempt) {
    NHttpRequest http;
    http.SetTimeout(kRoomRouterConnectMs, kRoomRouterReadMs);
    try {
      response = http.request("POST", url, request.dump(), headers);
      fetched  = true;
    }
    catch (const std::exception & e) {
      fetchError = e.what();
    }
    if (!fetched && attempt < kRoomRestoreFetchAttempts) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kRoomRestoreFetchPauseMs));
    }
  }
  if (!fetched) {
    NLogWarning("Cannot fetch the stored session of room '%s': %s", fRoomId.c_str(), fetchError.c_str());
    release(false);
    return;
  }

  if (response.status < 200 || response.status >= 300) {
    NLogWarning("The router answered HTTP %d for the stored session of room '%s'", response.status, fRoomId.c_str());
    release(false);
    return;
  }

  json reply;
  try {
    reply = json::parse(response.body);
  }
  catch (const json::parse_error &) {
    NLogWarning("The router returned an unreadable stored session for room '%s'", fRoomId.c_str());
    release(false);
    return;
  }

  // A failure here is the router refusing the request, not an absent session - say so, or a
  // broken fetch looks like a room that simply had nothing stored.
  if (NUtils::GetJsonString(JsonMember(reply, "result")) != "success") {
    NLogWarning("The router refused the stored session of room '%s': %s", fRoomId.c_str(),
                NUtils::GetJsonString(JsonMember(reply, "error")).c_str());
    release(false);
    return;
  }

  const json payload = JsonMember(reply, "payload");
  if (!NUtils::GetJsonBool(JsonMember(payload, "hasSnapshot"))) {
    NLogInfo("Room '%s' has no stored session to restore", fRoomId.c_str());
    release(true); // nothing stored: nothing to look for again
    return;
  }

  const json snapshot = JsonMember(payload, "snapshot");

  // Replay through our own request path, so the history, workspace and broadcasts behave
  // exactly as they do for a client - the route NMcpServer::CallTool already takes. Dispatched as
  // the server itself (a stated identity, empty because no client is behind a replay): that is
  // what keeps the gates from asking a request the server made of itself for a token - the
  // snapshot being replayed was stored from requests that already carried one.
  const NRoomSession::Dispatch dispatch = [this](const std::string & method, const std::string & route,
                                                 const json & body, std::string & dispatchError) -> json {
    auto arg = std::make_shared<THttpCallArg>();
    arg->SetMethod(method.c_str());
    arg->SetPathName("api");
    arg->SetFileName(route.c_str());
    arg->SetPostData(body.dump().c_str());

    ProcessRequestAs(arg, NRequestIdentity());

    std::string text;
    if (arg->GetContent() != nullptr && arg->GetContentLength() > 0) {
      text.assign(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
    }

    json parsed;
    try {
      parsed = text.empty() ? json() : json::parse(text);
    }
    catch (const json::parse_error &) {
      parsed = json();
    }

    if (!parsed.is_object() || NUtils::GetJsonString(JsonMember(parsed, "result")) != "success") {
      const std::string detail = NUtils::GetJsonString(JsonMember(parsed, "error"));
      dispatchError            = method + " " + route + " failed: " + (detail.empty() ? text : detail);
    }
    return parsed;
  };

  // A snapshot that carries a combination tree restores it directly: the tree is a record the server
  // adopts as-is, and the active combination is materialized (its nodes' POST handlers replayed) -
  // neither of which has a client-facing verb, which is why this is done here rather than through the
  // dispatcher. The tree is keyed by tool group (v3; a v2 snapshot is regrouped on restore). Older
  // snapshots, and sessions that declare no dependencies, fall through to the action replay below.
  const json combinations = JsonMember(snapshot, "combinations");
  if (Ndmspc::NInstanceTree::HasNodes(combinations)) {
    Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());
    tree.Restore(combinations);

    // The room's own session state comes back with it: the names it held (the nodes keep their ids, so
    // the names still belong to the right sessions) and what each session had drawn.
    AdoptSessionState(snapshot);

    const std::vector<std::string> roomActive = tree.Active();

    // Every group's own live chain comes back - its open file and the steps it was running - because
    // they are kept apart. A group with no live chain (a snapshot from before per-group chains) has
    // its first root materialized, which reopens its file. One group's chain never covers another's,
    // so no tool is left with "No ROOT file is open".
    std::map<std::string, std::vector<std::string>> wanted;
    for (const auto & rootId : tree.Roots()) {
      const std::string group = Ndmspc::NInstanceTree::GroupOf(tree.Action(rootId));
      if (group.empty() || wanted.count(group) != 0) continue;
      const auto active = tree.Active(group);
      wanted[group]     = active.empty() ? std::vector<std::string>{rootId} : active;
    }

    bool restored = !wanted.empty();
    for (const auto & entry : wanted) {
      json out;
      if (MaterializeCombination(entry.second, entry.first, out)) {
        NLogInfo("Room '%s' restored its %s session", fRoomId.c_str(), entry.first.c_str());
      }
      else {
        NLogWarning("Room '%s' could not restore its %s session: %s", fRoomId.c_str(), entry.first.c_str(),
                    NUtils::GetJsonString(JsonMember(out, "error")).c_str());
        restored = false;
      }
    }
    if (restored) {
      // Materializing made each group live; the room's own selected combination stands.
      tree.SetActive(roomActive);
      NLogInfo("Room '%s' restored its stored combinations", fRoomId.c_str());
      release(true);
      return;
    }
    // Fall through: the action replay below at least restores the single combination it describes.
  }

  std::string error;
  if (NRoomSession::RestoreInPlace(snapshot, dispatch, error)) {
    NLogInfo("Room '%s' restored its stored session", fRoomId.c_str());
    release(true);
    return;
  }

  NLogError("Room '%s' could not restore its stored session: %s", fRoomId.c_str(), error.c_str());
  release(false);
}

void NHttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{
  // A control request (cancel) is answered here, inline, before the action worker: it must not queue
  // behind the action it is cancelling.
  if (HandleControlRequest(arg)) return;

  // Page, static assets and the websocket engine's own frames (WS_CONNECT/WS_READY/WS_DATA/...) are
  // served on this thread, as they always were. Only /api actions run on the worker: routing the
  // engine's frames through it would run NWsHandler::ProcessWS on the worker too, so a running action
  // would block reading the cancel (and ProcessWS's RunAction would run inline and block the worker).
  const TString pathName = arg->GetPathName();
  const TString fileName = arg->GetFileName();
  TString       fullpath = TString::Format("/%s/%s/", pathName.Data(), fileName.Data());
  fullpath.ReplaceAll("//", "/");
  fullpath.ReplaceAll("//", "/");
  if (!fullpath.BeginsWith("/api/")) {
    Dispatch(std::move(arg), nullptr);
    return;
  }

  // Synchronous: the action runs on the worker and this waits for it, so the caller still reads the
  // reply content right after (the contract every HTTP/MCP/replay caller relies on).
  std::mutex              doneMutex;
  std::condition_variable done;
  bool                    finished = false;
  RunAction(std::move(arg), nullptr, "", [&]() {
    std::lock_guard<std::mutex> lock(doneMutex);
    finished = true;
    done.notify_one();
  });
  std::unique_lock<std::mutex> lock(doneMutex);
  done.wait(lock, [&]() { return finished; });
}

void NHttpServer::ProcessRequestAs(std::shared_ptr<THttpCallArg> arg, const NRequestIdentity & identity)
{
  std::mutex              doneMutex;
  std::condition_variable done;
  bool                    finished = false;
  auto                    stated   = std::make_shared<NRequestIdentity>(identity);
  RunAction(std::move(arg), std::move(stated), "", [&]() {
    std::lock_guard<std::mutex> lock(doneMutex);
    finished = true;
    done.notify_one();
  });
  std::unique_lock<std::mutex> lock(doneMutex);
  done.wait(lock, [&]() { return finished; });
}

void NHttpServer::RunAction(std::shared_ptr<THttpCallArg> arg, std::shared_ptr<NRequestIdentity> identity,
                            const std::string & requestId, const std::function<void()> & onComplete)
{
  NActionWorker * worker;
  {
    std::lock_guard<std::mutex> lock(fWorkerMutex);
    if (fActionWorker == nullptr) fActionWorker = new NActionWorker();
    worker = fActionWorker;
  }

  // Already on the worker: a handler dispatching another request (the MCP tool-call path, or a room
  // replay). Run it inline - submitting it would have the worker wait on itself and deadlock.
  if (worker->OnWorkerThread()) {
    Dispatch(arg, identity.get());
    if (onComplete) onComplete();
    return;
  }

  // The flag this request can be cancelled through. Registered before it is queued, so a cancel
  // arriving while it waits (not only while it runs) still reaches it.
  std::shared_ptr<std::atomic<bool>> flag;
  if (!requestId.empty()) {
    flag = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(fCancelMutex);
    fCancelFlags[requestId] = flag;
  }

  worker->Submit([this, arg, identity, requestId, flag, onComplete]() {
    if (flag && flag->load(std::memory_order_relaxed)) {
      // Cancelled while it was still queued: answer as cancelled without running the action.
      arg->SetContentType("application/json");
      arg->SetContent(json{{"result", "failure"}, {"code", "cancelled"}, {"error", "Cancelled"}}.dump());
    }
    else {
      if (flag) NCancellation::SetCurrent(flag.get());
      Dispatch(arg, identity.get());
      if (flag) NCancellation::ClearCurrent();
    }
    if (!requestId.empty()) {
      std::lock_guard<std::mutex> lock(fCancelMutex);
      fCancelFlags.erase(requestId);
    }
    if (onComplete) onComplete();
  });
}

bool NHttpServer::CancelRequest(const std::string & requestId)
{
  if (requestId.empty()) return false;
  std::lock_guard<std::mutex> lock(fCancelMutex);
  const auto                  it = fCancelFlags.find(requestId);
  if (it == fCancelFlags.end()) return false;
  it->second->store(true, std::memory_order_relaxed);
  NLogInfo("Cancelling action for request %s", requestId.c_str());
  return true;
}

bool NHttpServer::HandleControlRequest(const std::shared_ptr<THttpCallArg> & arg)
{
  if (!arg) return false;
  const TString path = arg->GetPathName();
  const TString file = arg->GetFileName();
  if (path != "api" || (file != "cancel" && file != "upload" && file != "files")) return false;

  const std::string level =
      RoomAccessRequired() ? RoomAccessLevel(RequestAccessToken(arg.get())) : std::string("rw");
  if (level.empty()) {
    arg->SetContentType("application/json");
    arg->SetContent(json{{"result", "failure"}, {"error", "this room's access token is required"}}.dump());
    return true;
  }

  // Upload one chunk of a file the browser is bringing into the room. Ephemeral: it lands in the
  // room's working directory and is gone when the pod scales to zero (a durable file is opened by URL).
  if (file == "upload") {
    if (level != "rw") {
      arg->SetContentType("application/json");
      arg->SetContent(json{{"result", "failure"}, {"error", "this room's access token is read-only"}}.dump());
      return true;
    }
    const std::string query = arg->GetQuery() != nullptr ? arg->GetQuery() : "";
    auto              param = [&query](const std::string & key) -> std::string {
      const std::string needle = key + "=";
      const size_t      at     = query.find(needle);
      if (at == std::string::npos) return "";
      const size_t from = at + needle.size();
      const size_t end  = query.find('&', from);
      return query.substr(from, end == std::string::npos ? std::string::npos : end - from);
    };
    std::string name = param("name");
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::string clean;
    for (const char c : name) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') clean += c;
    }
    if (clean.empty() || clean == "." || clean == "..") {
      arg->SetContentType("application/json");
      arg->SetContent(json{{"result", "failure"}, {"error", "invalid file name"}}.dump());
      return true;
    }
    const std::string offsetText = param("offset");
    const long long   offset     = offsetText.empty() ? 0 : std::atoll(offsetText.c_str());
    const bool        last       = param("last") == "1";

    const std::string dir   = gSystem->pwd();
    const std::string part  = dir + "/." + clean + ".part";
    const std::string final = dir + "/" + clean;
    const char *      data  = static_cast<const char *>(arg->GetPostData());
    const size_t      len   = arg->GetPostDataLength();

    // A file already in the room is not overwritten silently: the first chunk is refused with
    // `code=exists`, and the caller either uploads under another name or repeats with `force=1`.
    if (offset == 0 && param("force") != "1") {
      std::ifstream probe(final, std::ios::binary);
      if (probe.good()) {
        probe.close();
        json reply;
        reply["result"] = "failure";
        reply["code"]   = "exists";
        reply["file"]   = clean;
        arg->SetContentType("application/json");
        arg->SetContent(reply.dump());
        return true;
      }
    }

    std::ofstream out(part, std::ios::binary | (offset == 0 ? std::ios::trunc : std::ios::app));
    if (!out) {
      arg->SetContentType("application/json");
      arg->SetContent(json{{"result", "failure"}, {"error", "cannot write the file"}}.dump());
      return true;
    }
    if (len > 0 && data != nullptr) out.write(data, static_cast<std::streamsize>(len));
    out.close();
    // Only the finished file is visible: a half-upload is not openable.
    if (last) std::rename(part.c_str(), final.c_str());

    json reply;
    reply["result"] = "success";
    reply["file"]   = clean;
    reply["path"]   = final;
    reply["bytes"]  = static_cast<long long>(offset) + static_cast<long long>(len);
    arg->SetContentType("application/json");
    arg->SetContent(reply.dump());
    return true;
  }

  // The files in the room's working directory (what a browser brought in, and what the room made).
  // Ephemeral, like the uploads: gone when the pod scales to zero. Dotfiles (half-uploads) are hidden.
  if (file == "files") {
    const std::string dir = gSystem->pwd();
    json              list = json::array();
    std::error_code   ec;
    for (const auto & entry : std::filesystem::directory_iterator(dir, ec)) {
      const std::string name = entry.path().filename().string();
      if (name.empty() || name[0] == '.') continue;
      if (!entry.is_regular_file(ec)) continue;
      json item;
      item["name"]     = name;
      item["path"]     = entry.path().string();
      item["size"]     = static_cast<long long>(entry.file_size(ec));
      item["modified"] = 0;
      list.push_back(item);
    }
    json reply;
    reply["result"] = "success";
    reply["path"]   = dir;
    reply["files"]  = list;
    arg->SetContentType("application/json");
    arg->SetContent(reply.dump());
    return true;
  }

  json in = json::object();
  const char * postData = (const char *)arg->GetPostData();
  if (postData != nullptr) {
    try {
      in = json::parse(postData);
    }
    catch (const json::parse_error &) {
    }
  }
  const std::string requestId =
      in.contains("requestId") && in["requestId"].is_string() ? in["requestId"].get<std::string>() : std::string();
  const bool cancelled = CancelRequest(requestId);

  json out;
  out["result"]    = "success";
  out["cancelled"] = cancelled;
  arg->SetContentType("application/json");
  arg->SetContent(out.dump());
  return true;
}

namespace {

/// @brief Whether a request is for the server's default page - the page the app is loaded from.
///
/// THttpServer answers its default page for any request without a file name; only the app's own
/// page is ours to answer here, so a path that merely has no file name ("/ws/...", "/assets/...")
/// is left to THttpServer, which knows what to do with it.
bool IsDefaultPageRequest(THttpCallArg * arg)
{
  if (arg == nullptr) return false;
  const TString name = arg->GetFileName();
  if (name == "index.htm" || name == "default.htm") return true;
  const char * path = arg->GetPathName();
  return name.IsNull() && path != nullptr && *path == 0;
}

} // namespace

namespace {

/// @brief The request's own arguments, without the server-added keys (see Dispatch).
json NodeParams(const json & in)
{
  json params = json::object();
  if (!in.is_object()) return params;
  for (auto it = in.begin(); it != in.end(); ++it) {
    if (!it.key().empty() && it.key()[0] == '_') continue; // _query / _identity / _ws
    if (it.key() == "path") continue;                      // the node's position, not its params
    // The room's own "started, first step not run yet" marker: it is bookkeeping, never an argument a
    // tool is run with, so a form that carries it back must not keep a session looking unrun.
    if (it.key() == "pending") continue;
    params[it.key()] = it.value();
  }
  return params;
}

/// @brief The node ids a request names, or {} when it names none.
std::vector<std::string> RequestPath(const json & in)
{
  std::vector<std::string> path;
  if (in.is_object() && in.contains("path") && in["path"].is_array()) {
    for (const auto & id : in["path"]) {
      if (id.is_string()) path.push_back(id.get<std::string>());
    }
  }
  return path;
}

/// @brief The deepest active node whose action is `action`, as a path ({} when none).
std::vector<std::string> ActivePathFor(Ndmspc::NInstanceTree & tree, const std::string & action)
{
  // The action's own group's live chain: a pathless GET/PATCH/DELETE targets the node of that action
  // in the group it belongs to, not whatever combination another group happens to have live.
  const auto active = tree.Active(Ndmspc::NInstanceTree::GroupOf(action));
  for (auto it = active.rbegin(); it != active.rend(); ++it) {
    if (tree.Action(*it) == action) return tree.Path(*it);
  }
  return {};
}

/// @brief Resolve the parent node a POST attaches to, against the dependency template.
bool ResolveParentInstance(Ndmspc::NInstanceTree & tree, const std::string & action, const json & in,
                           std::string & parentId, std::string & required, std::string & error)
{
  const std::string need = Ndmspc::NInstanceTree::ParentActionFor(action);
  std::vector<std::string> path = RequestPath(in);
  if (path.empty() && !need.empty()) path = ActivePathFor(tree, need);

  if (need.empty()) {
    if (!path.empty()) {
      error = action + " is a root action and cannot be nested";
      return false;
    }
    parentId = "";
    return true;
  }
  if (path.empty()) {
    // The prerequisite simply has not been created: report it as a missing prerequisite so a client
    // is told which action to run, exactly as the flat history case does.
    required = need;
    error    = "no " + need + " to attach to; create one first";
    return false;
  }
  const std::string id = path.back();
  if (!tree.Has(id)) {
    error = "unknown combination node '" + id + "'";
    return false;
  }
  if (tree.Action(id) != need) {
    error = action + " must sit under a " + need + ", not a " + tree.Action(id);
    return false;
  }
  parentId = id;
  return true;
}

/// @brief Resolve the node a PATCH/DELETE/GET targets.
bool ResolveTargetInstance(Ndmspc::NInstanceTree & tree, const std::string & action, const json & in,
                           std::string & nodeId, std::string & error)
{
  std::vector<std::string> path = RequestPath(in);
  if (path.empty()) path = ActivePathFor(tree, action);
  if (path.empty()) {
    error = "no " + action + " node; run it first";
    return false;
  }
  const std::string id = path.back();
  if (!tree.Has(id)) {
    error = "unknown combination node '" + id + "'";
    return false;
  }
  if (tree.Action(id) != action) {
    error = "node '" + id + "' is a " + tree.Action(id) + ", not a " + action;
    return false;
  }
  nodeId = id;
  return true;
}

} // namespace

void NHttpServer::Dispatch(std::shared_ptr<THttpCallArg> arg, const NRequestIdentity * statedIdentity)
{

  // NLogInfo("NHttpServer::ProcessRequest");

  NRequestIdentity identity;                    ///< Who this request runs as, once it is known
  const bool        identityStated = statedIdentity != nullptr;
  if (identityStated) identity = *statedIdentity;

  TString method   = arg->GetMethod();
  TString path     = arg->GetPathName();
  TString filename = arg->GetFileName();
  // if (arg->GetRequestHeader("Content-Type").CompareTo("application/json")) {
  //   // NLogWarning("Unsupported Content-Type: %s", arg->GetRequestHeader("Content-Type").Data());
  //   THttpServer::ProcessRequest(arg);
  //   return;
  // }

  NLogTrace("Received %s request for path: %s filename: %s", method.Data(), path.Data(), filename.Data());

  // A room wakes as an empty process: bring back the session it had before it scaled to zero
  // before serving the request that woke it, so the very first request already sees it. This
  // is a cheap check once it has run.
  RoomSessionRestoreOnce();

  TString fullpath = TString::Format("/%s/%s/", path.Data(), filename.Data()).Data();
  fullpath.ReplaceAll("//", "/");
  // Yes it needs to be done twice to handle cases where both path and filename are empty resulting in "///"
  fullpath.ReplaceAll("//", "/");

  NLogTrace("Constructed full path: %s", fullpath.Data());
  // if fullpath does not start with "/api" or "api", process it with base class handler

  if (!(fullpath.BeginsWith("/api/"))) {
    // A room is entered through its page, so the page needs the access token too - otherwise a
    // stale link would still open a session. Its own assets are left alone (they are inert, and a
    // reload re-fetches them with the cookie the page handed the browser), and so is the
    // websocket, whose upgrade carries its own check.
    const std::string page    = fullpath.Data();
    const bool        isAsset = page.rfind("/assets/", 0) == 0 || page.rfind("/ws", 0) == 0;
    if (!isAsset && !ApplyRoomAccess(arg.get(), method.Data(), /*isPage=*/true)) return;

    // The page carries this deployment's VITE_* settings (see PrepareRuntimeEnvPage). It is
    // answered here rather than by THttpServer so it can be marked no-store: the values are the
    // deployment's, and a page cached from an earlier rollout must not keep serving them.
    if (!fRuntimeEnvPage.empty() && IsDefaultPageRequest(arg.get())) {
      arg->SetContent(std::string(fRuntimeEnvPage));
      arg->SetContentType("text/html");
      arg->AddNoCacheHeader();
      return;
    }

    NLogTrace("Using base http server for path: %s", fullpath.Data());
    THttpServer::ProcessRequest(arg);
    return;
  }

  fullpath.Remove(0, 4);
  fullpath          = fullpath.Strip(TString::kLeading, '/');
  fullpath          = fullpath.Strip(TString::kTrailing, '/');
  std::string query = arg->GetQuery();
  NLogTrace("Processing %s request for path: %s query: %s", method.Data(), fullpath.Data(), query.c_str());

  // Enforce OIDC bearer authentication on plain HTTP /api requests.
  // Requests bridged from an authenticated WebSocket connection (nonzero WS id)
  // carry their identity already and are not re-checked here. The root info and
  // inspector-schema endpoints stay anonymous so UIs can bootstrap.
  //
  // room/state is the internal room-to-router channel and stays out of this check too: a room
  // has no user token to present - it reports its session on its own behalf. It authenticates
  // with the access token it was created with, which the router checks against the room the
  // request names (see NRoomRouter::HandleState).
  //
  // A request the server dispatches itself is not a request from the network at all, so there is
  // nothing to authenticate here: a tool call runs as the caller whose request already passed this
  // gate (see NMcpServer::CallTool), and a room replaying its stored session (ProcessRequestAs
  // from RoomSessionRestoreOnce) has no client behind it. Re-checking those rejected the very
  // requests the server had just admitted.
  const bool isWsBridged = arg->GetWSId() != 0;
  const bool isRoomState = fullpath == "room/state";
  if (fOidcVerifier && !isWsBridged && !identityStated && !isRoomState && !fullpath.IsNull() &&
      fullpath != "openapi/inspector" && fullpath != "inspector/openapi") {
    NOidcSession session;
    if (!NOidcHttpAuthenticator::ApplyToRequest(fOidcVerifier, arg.get(), &session)) {
      NLogDebug("OIDC authentication failed for %s request to /api/%s", method.Data(), fullpath.Data());
      return;
    }
    if (!identityStated) identity = NRequestIdentity::FromSession(session);
  }

  // Where nothing verified the caller itself, two weaker sources remain: an authenticating front
  // door that verified them and forwarded what it found, and a user name something authenticated
  // earlier (a bridged WebSocket connection). A request that offers neither carries no identity, and
  // an action that needs one falls back to what the client asserts.
  if (!identityStated && identity.Empty()) {
    if (fTrustForwardedIdentity) identity = NRequestIdentity::FromForwardedHeaders(arg.get());
    if (identity.Empty() && arg->GetUserName() != nullptr) {
      identity = NRequestIdentity::FromUsername(arg->GetUserName());
    }
  }

  // The room's own gate, on every /api request including the bridged, MCP and replay ones - which
  // already carry their identity, so it passes them through.
  if (!ApplyRoomAccess(arg.get(), method.Data(), /*isPage=*/false, /*alreadyAdmitted=*/identityStated)) return;

  json out;
  json wsOut;
  if (fullpath.IsNull()) {

    out["result"]  = "success";
    out["message"] = "Welcome to NHttpServer API";
    // out["ws"]["path"] = "ws/root.websocket";

    out["state"]["history"]   = GetJson();
    out["state"]["users"]     = fNWsHandler ? fNWsHandler->GetClientCount() : 0;
    out["state"]["workspace"] = GetWorkspace();
    out["state"]["combinations"] = Ndmspc::NInstanceTree(GetCombinations()).ToTree();

    // Server identity (same string as the CLI --version banner).
    out["state"]["server"]["name"]    = NDMSPC_NAME;
    out["state"]["server"]["version"] = std::string(NDMSPC_VERSION) + "-" + NDMSPC_VERSION_RELEASE;

    // Expose current authentication mode to clients (bootstrap endpoint stays
    // anonymous so UIs can discover whether a token is required).
    out["state"]["authentication"]["enabled"] = fOidcVerifier != nullptr;
    if (arg->GetUserName()) {
      out["state"]["authentication"]["username"] = arg->GetUserName();
    }
    if (fOidcVerifier) {
      out["state"]["authentication"]["type"] = "bearer";
    }

    // Derive group from handler keys if not yet set by a handler call
    if (fGroup.empty()) {
      const auto handlersCopy = GetHttpHandlers();
      for (const auto & h : handlersCopy) {
        auto pos = h.first.find('/');
        if (pos != std::string::npos) {
          fGroup = h.first.substr(0, pos);
          break;
        }
      }
    }
    if (!fGroup.empty()) {
      out["state"]["group"] = fGroup;
    }
  }
  else {

    std::string rawContent;
    {
      const char * postData = (const char *)arg->GetPostData();
      if (postData != nullptr) rawContent = postData;
    }

    // Special-case: MCP (Model Context Protocol) endpoint. Each JSON-RPC message is
    // handled in-process; tool calls are routed back through ProcessRequest, so the
    // action's own history/workspace/broadcast bookkeeping already happened and this
    // envelope level must not repeat it. The raw body is handed to the MCP server
    // (before the generic parse below) so malformed JSON yields a JSON-RPC parse
    // error (-32700), matching the stdio transport.
    if (fullpath == "mcp") {
      if (!fMcpEnabled) {
        NLogDebug("Rejecting /api/mcp request: MCP endpoint is disabled");
        arg->SetContentType("application/json");
        arg->SetContent("{\"error\": \"MCP endpoint is disabled\"}");
        return;
      }
      Ndmspc::NMcpServer mcp(this);
      // A tool call is dispatched as a request of its own (see NMcpServer::CallTool), so it runs as
      // the caller that reached this endpoint - which is what lets a per-user action work over MCP.
      mcp.SetCallerIdentity(identity);
      json              response = rawContent.empty() ? mcp.Handle(json(nullptr)) : mcp.HandleText(rawContent);

      arg->AddHeader("Access-Control-Allow-Origin", GetCors());
      if (!rawContent.empty()) {
        try {
          const json message = json::parse(rawContent);
          if (message.is_object() && message.value("method", "") == "initialize") {
            arg->AddHeader("Mcp-Session-Id",
                           TString::Format("ndmspc-%p-%lld", static_cast<void *>(this),
                                           static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()))
                               .Data());
          }
        }
        catch (const json::parse_error &) {
        }
      }
      arg->SetContentType("application/json");
      arg->SetContent(response.is_null() ? "" : response.dump());
      return;
    }

    json in;
    try {
      if (!rawContent.empty()) in = json::parse(rawContent);
    }
    catch (json::parse_error & e) {
      NLogError("JSON parse error: %s", e.what());
      arg->SetContentType("application/json");
      arg->SetContent("{\"error\": \"Invalid JSON format\"}");
      return;
    }

    if (!query.empty()) {
      if (in.is_null()) in = json::object();
      if (in.is_object()) {
        in["_query"] = query;
        NLogTrace("Passing query to HTTP handler: %s", query.c_str());
      }
    }

    // The caller's identity travels with the request, in the same place its query does: a handler is
    // only ever handed its method and its input, and an action that is per-user (the room router) has
    // to know who is asking. Assigned here, after the body was parsed and from what the server itself
    // established, so a client cannot write its own.
    if (!identity.Empty() && in.is_null()) in = json::object();
    if (in.is_object()) in["_identity"] = identity.ToJson();

    // The connection a request came over, when it came over a websocket: the room router pushes to a
    // watcher by connection, so an action that registers one has to know which connection asked.
    // Zero (an ordinary HTTP request) is not stated at all.
    if (arg->GetWSId() != 0 && in.is_null()) in = json::object();
    if (in.is_object() && arg->GetWSId() != 0) in["_ws"] = static_cast<long>(arg->GetWSId());

    NLogTrace("Received %s request with content: %s", method.Data(), in.dump().c_str());

    // Special-case: provide an OpenAPI-compatible inspector schema endpoint
    const bool isInspectorSchema = fullpath == "openapi/inspector" || fullpath == "inspector/openapi";
    if (isInspectorSchema) {
      json openapi;
      openapi["openapi"] = "3.0.0";
      openapi["info"]["title"] = std::string("NGn Inspector for ") + GetName();
      openapi["info"]["version"] = "1.0.0";
      // Put the inspector schema under components.schemas.N_Workspace
      json inspectorSchema = GetInspectorSchema();
      // If inspector contains properties, use that as the schema, otherwise include whole inspector
      if (!inspectorSchema["inspector"]["properties"].is_null()) {
        openapi["components"]["schemas"]["N_Workspace"] = inspectorSchema["inspector"]["properties"];
      }
      else {
        openapi["components"]["schemas"]["N_Workspace"] = inspectorSchema;
      }
      out = openapi;
    }
    else {
      // Resolve the registered handler without inserting (a request-time
      // insertion would mutate the map and race with concurrent lookups).
      const Ndmspc::NHttpFuncPtr handlerFn = FindHttpHandler(fullpath.Data());
      if (handlerFn == nullptr) {
        NLogError("Unsupported action: %s", fullpath.Data());
        arg->SetContentType("application/json");
        arg->SetContent("{\"error\": \"Unsupported action\"}");
        return;
      }

      const std::string action   = fullpath.Data();
      const bool        isPost   = !method.CompareTo("POST");
      const bool        isDelete = !method.CompareTo("DELETE");
      // A group whose tools declare dependencies is a combination tree (NInstanceTree): each
      // request names the node it belongs to, and only the selected combination is live at a time.
      const bool            nodeAction = Ndmspc::NInstanceTree::IsNodeAction(action);
      Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

      bool        runHandler = true;
      std::string parentId; ///< POST: the parent to attach to ("" = a new root)
      std::string nodeId;   ///< PATCH/DELETE/GET: the node the request targets
      std::string required; ///< When the refusal is a missing prerequisite, the action to run first
      std::string created;  ///< A POST's own node, created before its handler so its session is known
      std::string filled;   ///< The fresh session root this POST fills instead of creating a node
      fCurrentInstance = "";
      fCurrentSession  = "";

      if (nodeAction) {
        std::string error;
        if (isPost) {
          if (!ResolveParentInstance(tree, action, in, parentId, required, error)) {
            out["result"] = "failure";
            if (!required.empty()) {
              out["code"]     = "prerequisite_required";
              out["required"] = required;
              out["error"]    = "Requires " + required + " to be run first";
            }
            else {
              out["code"]  = "invalid_combination";
              out["error"] = error;
            }
            runHandler = false;
          }
        }
        else if (!ResolveTargetInstance(tree, action, in, nodeId, error)) {
          out["result"] = "failure";
          out["code"]   = "invalid_combination";
          out["error"]  = error;
          runHandler    = false;
        }

        // Make the combination this request belongs to the live one.
        // Only a request that acts *inside* a combination materializes one. A root POST opens a new
        // session: it has nothing to bring live - its own node is created below and the objects it
        // makes are keyed by the session - so it neither truncates another session nor another session
        // of its own group. That is what lets several browser files (or analyses) be open at once.
        const std::vector<std::string> want = isPost ? tree.Path(parentId) : tree.Path(nodeId);
        if (runHandler && !want.empty() &&
            !MaterializeCombination(want, Ndmspc::NInstanceTree::GroupOf(action), out)) {
          runHandler = false;
        }
        if (runHandler) {
          if (isPost) {
            // A run that lands on a session the room has just started - nothing in its arguments but
            // its token - fills that session's root rather than adding a sibling beside it: this run is
            // the session's first step. It is what lets two sessions of one group hold the same file,
            // each its own root and neither replacing the other.
            if (parentId.empty()) filled = FreshSessionRoot(action);

            if (!filled.empty()) {
              fCurrentInstance = filled;
            }
            // Know the session before the handler makes anything: a POST's node is created here - the
            // same node it would have been created as below, on success - so the objects and defaults a
            // handler makes are keyed by the session they belong to. A handler that fails has its node
            // taken back below, so a refused POST still leaves nothing behind.
            else {
              created          = tree.Create(action, NodeParams(in), parentId, "");
              fCurrentInstance = created;
            }
          }
          else {
            fCurrentInstance = nodeId;
          }
          fCurrentSession = fCurrentInstance.empty() ? std::string() : tree.Path(fCurrentInstance).front();
        }
      }
      else {
        // An ordinary action - the browser's internal `rbrowser/*`, say - belongs to the session its
        // `path` names: the node it acts on.
        const std::vector<std::string> at = RequestPath(in);
        if (!at.empty() && tree.Has(at.front())) fCurrentSession = at.front();
      }

      if (runHandler) {
        // A tool may declare prerequisites (NMcpToolInfo::dependsOn). For a node action they were
        // just materialized, so this passes; for anything else the flat history says whether the
        // prerequisite has run. Either way the refusal names the action to run first.
        const std::string missing = UnmetPrerequisite(action);
        if (!missing.empty()) {
          NLogWarning("Refusing %s: prerequisite %s has not run", action.c_str(), missing.c_str());
          out["result"]   = "failure";
          out["code"]     = "prerequisite_required";
          out["required"] = missing;
          out["error"]    = "Requires " + missing + " to be run first";
        }
        else {
          // Roll back any existing entry for this route (and every newer entry)
          // before running the handler. Their DELETE handlers delete the stale
          // in-memory objects and close the underlying files. Doing this first
          // prevents those DELETE handlers from tearing down the objects this
          // request is about to create under the same keys.
          if (fUseHistory && isPost) {
            fWorkspace.RemoveEntry(fullpath.Data());
          }

          handlerFn(method.Data(), in, out, wsOut, fObjectsMap);
        }
      }

      // Record the outcome in the combination tree, and tell clients about it.
      if (nodeAction) {
        const bool ok = out.contains("result") && out["result"].is_string() &&
                        out["result"].get<std::string>() == "success";
        if (!filled.empty()) {
          if (runHandler && ok) {
            // It takes the arguments it has just run with, and keeps its token, so it is still that
            // session - and keeps its id, so the room stays on it (see FreshSessionRoot).
            json params       = NodeParams(in);
            params["session"] = tree.Params(filled).value("session", std::string());
            tree.SetParams(filled, params);
            out["combination"]["id"]   = filled;
            out["combination"]["path"] = tree.Path(filled);
          }
          else {
            // A refused or failed first step leaves the session as it was: started, and still empty.
            fCurrentInstance.clear();
            fCurrentSession.clear();
          }
        }
        else if (isPost && !created.empty()) {
          if (runHandler && ok) {
            const std::vector<std::string> path = tree.Path(created);
            tree.SetActive(path);
            // A root is a session, so a plain `open` starts a named one too - the room names it, and a
            // picker then reads a name rather than whichever file it happens to have opened. A node that
            // already had a name keeps it: re-running a step is not a new session, and a rename stands.
            if (parentId.empty() && fSessionNames.count(created) == 0) {
              fSessionNames[created] = SessionNameFor(Ndmspc::NInstanceTree::GroupOf(action), created);
            }
            out["combination"]["id"]   = created;
            out["combination"]["path"] = path;
          }
          else {
            // A refused or failed POST leaves no node - and so no session behind it.
            tree.RemoveSubtree(created, nullptr);
            fCurrentInstance.clear();
            fCurrentSession.clear();
          }
        }
        if (runHandler && isDelete && ok && !nodeId.empty()) {
          tree.RemoveSubtree(nodeId, nullptr);
        }
        if (runHandler) {
          wsOut["payload"]["combinations"] = tree.ToTree();
          out["combinations"]              = tree.ToTree();
          // The room's sessions, and the one it is on, ride along: any step that starts a session makes
          // one (a root is a session), so a client has to be told when the list grows - not only when
          // the `session` action runs, or a session started by a plain `open` would have no name in the
          // bar until something else happened to refresh it.
          const json sessions              = SessionList();
          wsOut["payload"]["session"]      = sessions["session"];
          wsOut["payload"]["sessions"]     = sessions["sessions"];
        }
      }
    }

    // fObjectsMap["_httpServer"] = this;

    NHistoryEntry * historyEntry = nullptr;
    if (fUseHistory) {
      if (!method.CompareTo("POST")) {
        NLogTrace("Adding history entry for path: %s", fullpath.Data());
        historyEntry = new NHistoryEntry(fullpath.Data(), method.Data());
        historyEntry->SetPayloadIn(in);
        NLogTrace("History entry created for path: %s with payload: %s", fullpath.Data(), in.dump().c_str());
        fWorkspace.AddEntry(historyEntry);
      }
    }

    if (fUseHistory) {
      NLogTrace("HTTP handler output for path %s: %s", fullpath.Data(), out.dump().c_str());
      if (!out["result"].is_null() && !out["result"].get<std::string>().compare("success")) {
        if (!method.CompareTo("POST")) {
          if (historyEntry) {
            historyEntry->SetPayloadOut(out);
            historyEntry->SetPayloadWsOut(wsOut);
          }
        }
        else if (!method.CompareTo("DELETE")) {
          fWorkspace.RemoveEntry(fullpath.Data());
        }
      }
      else {
        if (!method.CompareTo("POST")) {
          fWorkspace.RemoveEntry(static_cast<int>(fWorkspace.GetEntries().size()) - 1);
        }
      }
    }

    // A room reports its session to the router after a request that may have changed it, so
    // a room which later scales to zero can be brought back. POSTs and PATCHes are what change
    // a session - a PATCH carries the drill-down point, and no POST sets it - and the report
    // is skipped when nothing changed.
    if (!method.CompareTo("POST") || !method.CompareTo("PATCH")) RoomSessionPush();

    // Don't broadcast workspace updates in response to DELETE requests
    if (!method.CompareTo("DELETE")) {
      wsOut["workspace"] = nullptr;
    }

    // Check for suppression header from client: when present, avoid broadcasting
    // workspace/state/config to websocket clients for this request. For now we
    // only support a comma-separated list of workspace keys to suppress. Do
    // not support suppressing the entire workspace (boolean values are ignored).
    std::set<std::string> suppressedWorkspaceKeys;
    try {
      TString hdr = arg->GetRequestHeader("X-Ndmspc-Suppress-Workspace-Publish");
      if (!hdr.IsNull()) {
        std::string v = hdr.Data();
        // normalize to lowercase
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Do not support whole-workspace suppression for now; if a boolean
        // value was provided, log and ignore it.
        // if (v == "1" || v == "true" || v == "yes") {
        //   NLogDebug("Boolean workspace suppression (true/1/yes) is not supported; ignoring header value");
        // }

        // parse comma-separated list of keys to suppress (if any)
        std::stringstream ss(v);
        std::string token;
        while (std::getline(ss, token, ',')) {
          // trim whitespace from token
          auto l = token.find_first_not_of(" \t\n\r");
          if (l == std::string::npos) continue;
          auto r = token.find_last_not_of(" \t\n\r");
          std::string key = token.substr(l, r - l + 1);
          if (!key.empty() && key != "1" && key != "true" && key != "yes") suppressedWorkspaceKeys.insert(key);
        }
      }
    } catch (...) {
      suppressedWorkspaceKeys.clear();
    }

    if (!suppressedWorkspaceKeys.empty()) {
      NLogDebug("Suppressing workspace keys from broadcast due to X-Ndmspc-Suppress-Workspace-Publish header");
    }

    // A drawing is the room's own to remember: a client that joins later, or a view switched to this
    // session, is shown it again (see SessionState). It is kept under the **session** that drew it -
    // two sessions of one tool each keep their own pads - falling back to the group for a request that
    // names none. The group and session ride on the frame too, for the client's routing only: they say
    // where the drawing belongs, not that the room moved there (the `group`/`session` actions say that).
    if (!wsOut["payload"].is_null() && wsOut["payload"].is_object() && wsOut["payload"].contains("pad")) {
      const std::string group = (wsOut.contains("group") && wsOut["group"].is_string())
                                    ? wsOut["group"].get<std::string>()
                                    : fGroup;
      if (!group.empty()) wsOut["payload"]["group"] = group;
      const std::string drewIn = fCurrentSession.empty() ? group : fCurrentSession;
      if (!drewIn.empty()) wsOut["payload"]["session"] = drewIn;
      RecordPads(drewIn, wsOut["payload"]["pad"]);
    }

    if (!wsOut["payload"].is_null() || !wsOut["workspace"].is_null() || !wsOut["state"].is_null()) {
      json wsMessage;
      wsMessage["event"]   = "message";
      wsMessage["payload"] = wsOut["payload"].is_null() ? json::object() : wsOut["payload"];
      // If state is present, include it in the same payload
      if (!wsOut["state"].is_null()) {
        wsMessage["payload"]["state"] = wsOut["state"];
      }

      // The schemas this frame publishes, for the session it belongs to: what that session already has,
      // plus what this frame changes. Defaults are kept per session, so two sessions of a tool never
      // share an `open` default.
      if (!wsOut["workspace"].is_null()) {
        // The group the handler names, else the room's own group - it is what a client grouping by
        // group (rather than by session) is handed.
        const std::string frameGroup = (wsOut.contains("group") && wsOut["group"].is_string())
                                           ? wsOut["group"].get<std::string>()
                                           : fGroup;

        json workspace = GetWorkspace();

        for (auto it = wsOut["workspace"].begin(); it != wsOut["workspace"].end(); ++it) {
          NLogTrace("Updating workspace entry for: %s", it.key().c_str());
          // if value is null, skip it
          if (it.value().is_null()) {
            NLogTrace("Skipping null workspace entry for: %s", it.key().c_str());
            continue;
          }
          // skip suppressed keys
          if (suppressedWorkspaceKeys.find(it.key()) != suppressedWorkspaceKeys.end()) {
            NLogTrace("Skipping suppressed workspace key from wsOut: %s", it.key().c_str());
            continue;
          }

          workspace[it.key()]          = it.value();
          workspace[it.key()]["type"] = "object";
          GetWorkspace()[it.key()]    = it.value();         // this session (what a tool's ctx reads)
          fWorkspace.GetWorkspace()[it.key()] = it.value(); // the flat view older readers use
          // Kept per group too, for a client that keys its defaults by group (see SessionState).
          if (!frameGroup.empty()) fWorkspaceByGroup[frameGroup][it.key()] = it.value();
        }
        wsMessage["payload"]["workspace"]["schema"]["properties"] = workspace;

        // The session it belongs to, so a client can keep each session's defaults apart.
        if (!fCurrentSession.empty()) wsMessage["payload"]["workspace"]["schema"]["session"] = fCurrentSession;

        // Pass through group prefix if set by handler macro
        if (wsOut.contains("group") && wsOut["group"].is_string()) {
          wsMessage["payload"]["workspace"]["schema"]["group"] = wsOut["group"];
          if (fGroup.empty()) {
            fGroup = wsOut["group"].get<std::string>();
          }
        }
      }

      NUtils::RawJsonInjections injections;
      if (NUtils::CollectRawJsonInjections(wsOut, injections)) {
        std::string wsMessageStr = NUtils::InjectRawJson(wsMessage, injections);
        NLogDebug("Broadcasting to WebSocket clients for path %s: %s", fullpath.Data(), wsMessageStr.c_str());
        if (fNWsHandler) {
          fNWsHandler->BroadcastUnsafe(wsMessageStr);
        }
      }
      else {
        NLogDebug("Broadcasting to WebSocket clients for path %s: %s", fullpath.Data(), wsMessage.dump().c_str());
        WebSocketBroadcast(wsMessage);
      }
    }
    else {
      NLogTrace("Skipping WebSocket broadcast for path %s: no payload or workspace changes", fullpath.Data());
    }
  }

  // arg->AddHeader("X-Header", "Test");
  arg->AddHeader("Access-Control-Allow-Origin", GetCors());
  arg->SetContentType("application/json");
  arg->SetContent(out.dump());
  // arg->SetContent("ok");
  // arg->SetContentType("text/plain");
}

std::string NHttpServer::UnmetPrerequisite(const std::string & action) const
{
  if (gNdmspcMcpTools == nullptr) return {};

  const auto info = gNdmspcMcpTools->find(action);
  if (info == gNdmspcMcpTools->end()) return {};

  for (const auto & dep : info->second.dependsOn) {
    // A tool that names itself, or names nothing, declares no prerequisite: skip it rather than
    // refusing the tool forever.
    if (dep.empty() || dep == action) continue;
    // A prerequisite that is not registered here (a tool another macro would provide) is not
    // enforced, so a macro stays loadable on its own.
    if (gNdmspcMcpTools->find(dep) == gNdmspcMcpTools->end()) continue;
    if (!fWorkspace.HasEntry(dep)) return dep;
  }
  return {};
}

bool NHttpServer::MaterializeCombination(const std::vector<std::string> & path, const std::string & group, json & out)
{
  Ndmspc::NInstanceTree tree(fWorkspace.GetCombinations());

  // Only this group's live chain moves: another group's combination - and the file it has open - is its
  // own, and materializing this one must leave it alone. The removal is scoped to the route's group
  // (NWorkspace::RemoveEntry), so taking this group's divergent entry takes its chain with it and
  // nothing else. `fEntries` holds one entry per live node; if that ever disagrees with the group's
  // active path, rebuild from scratch rather than truncate the wrong node.
  const auto                   active = tree.Active(group);
  std::vector<NHistoryEntry *> mine;
  for (auto * entry : fWorkspace.GetEntries()) {
    if (Ndmspc::NInstanceTree::GroupOf(entry->GetName()) == group) mine.push_back(entry);
  }

  size_t keep = 0;
  while (keep < active.size() && keep < path.size() && active[keep] == path[keep]) keep++;
  if (mine.size() != active.size()) {
    if (!mine.empty()) fWorkspace.RemoveEntry(mine.front()->GetName());
    keep = 0;
  }
  else if (keep < mine.size()) {
    fWorkspace.RemoveEntry(mine[keep]->GetName());
  }

  // Replay what is not live yet.
  for (size_t i = keep; i < path.size(); ++i) {
    const std::string id = path[i];
    if (!tree.Has(id)) {
      out["result"] = "failure";
      out["code"]   = "invalid_combination";
      out["error"]  = "unknown combination node '" + id + "'";
      return false;
    }
    const std::string          action = tree.Action(id);
    const Ndmspc::NHttpFuncPtr fn     = FindHttpHandler(action);
    if (fn == nullptr) {
      out["result"] = "failure";
      out["code"]   = "invalid_combination";
      out["error"]  = "no handler for '" + action + "'";
      return false;
    }

    fCurrentInstance = id;
    json params      = tree.Params(id);
    json nodeOut, nodeWsOut;
    fn("POST", params, nodeOut, nodeWsOut, fObjectsMap);
    if (!(nodeOut.contains("result") && nodeOut["result"].is_string() &&
          nodeOut["result"].get<std::string>() == "success")) {
      out["result"] = "failure";
      out["code"]   = "materialize_failed";
      out["error"]  = action + " could not be restored: " + nodeOut.value("error", std::string("failed"));
      fCurrentInstance = "";
      return false;
    }
    auto * entry = new NHistoryEntry(action.c_str(), "POST");
    entry->SetPayloadIn(params);
    entry->SetPayloadOut(nodeOut);
    entry->SetPayloadWsOut(nodeWsOut);
    fWorkspace.AddEntry(entry);

    // A node's stored point comes back with it, so the live state matches the combination.
    const json state = tree.State(id);
    if (state.is_object() && state.contains("point")) fWorkspace.GetState()["spectra"]["point"] = state["point"];
  }

  tree.SetActive(group, path);
  fCurrentInstance = path.empty() ? "" : path.back();
  return true;
}

TObject * NHttpServer::GetInputObject(const std::string & name)
{
  if (fObjectsMap.find(name) != fObjectsMap.end()) {
    return fObjectsMap[name];
  }
  return nullptr;
}

void NHttpServer::ResetServer()
{
  ///
  /// Clear workspace history first so DELETE handlers can still access input
  /// objects, then remove any remaining objects that weren't cleaned up by
  /// the handlers.
  ///
  NLogInfo("NHttpServer::ResetServer: Clearing history ...");
  ClearHistory();
  NLogInfo("NHttpServer::ResetServer: Removing remaining input objects ...");
  std::vector<std::string> keys;
  keys.reserve(fObjectsMap.size());
  for (const auto & pair : fObjectsMap) {
    keys.push_back(pair.first);
  }
  for (const auto & key : keys) {
    NLogInfo("NHttpServer::ResetServer: Removing input object '%s'", key.c_str());
    RemoveInputObject(key);
  }
  NLogInfo("NHttpServer::ResetServer: Done.");
}

bool NHttpServer::RemoveInputObject(const std::string & name)
{
  if (fObjectsMap.find(name) != fObjectsMap.end()) {
    TObject * obj = fObjectsMap[name];
    if (obj) {
      NLogDebug("Removing input object: %s", name.c_str());
      delete obj;
    }
    fObjectsMap.erase(name);
  }

  bool stillExists = (fObjectsMap.find(name) != fObjectsMap.end());
  return !stillExists;
}
// For compatibility, provide GetJson and Export/Load wrappers
json NHttpServer::GetJson() const
{
  json historyJson = json::array();

  for (const auto & entry : fWorkspace.GetEntries()) {
    json entryJson;
    entryJson["name"]             = entry->GetName();
    entryJson["method"]           = "POST";
    entryJson["payload"]["in"]    = entry->GetPayloadIn();
    entryJson["payload"]["out"]   = entry->GetPayloadOut();
    entryJson["payload"]["wsOut"] = entry->GetPayloadWsOut();
    historyJson.push_back(entryJson);
  }
  return historyJson;
}

} // namespace Ndmspc
