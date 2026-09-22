#include <TROOT.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#include <THttpCallArg.h>
#include <THttpServer.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NHistoryEntry.h"
#include "ndmspc/http/NHttpRequest.h"
#include "ndmspc/http/NMcpServer.h"
#include "ndmspc/http/NOidcHttpAuthenticator.h"
#include "ndmspc/http/NRoomAccess.h"
#include "ndmspc/http/NRoomSession.h"
#include "ndmspc/ndmspc.h"
#include "NHttpServer.h"

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

std::string NHttpServer::RequestAccessToken(THttpCallArg * arg) const
{
  if (arg == nullptr) return {};

  // The link carries the token; a script may send it as a header; a browser is given a cookie by
  // the page it loaded, because a page's own scripts cannot add a header to their API calls.
  const char * query = arg->GetQuery();
  std::string  token = NRoomAccess::TokenFromQuery(query != nullptr ? query : "");
  if (!token.empty()) return token;

  token = arg->GetRequestHeader(NRoomAccess::kHeader).Data();
  if (!token.empty()) return token;

  return NRoomAccess::TokenFromCookie(arg->GetRequestHeader("Cookie").Data());
}

bool NHttpServer::ApplyRoomAccess(THttpCallArg * arg, const std::string & method, bool isPage)
{
  if (arg == nullptr || !RoomAccessRequired()) return true;

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

  return NRoomSession::Build(fRoomId, file, history, point);
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

  const std::string file = snapshot.value("file", std::string());
  const std::string url  = fRoomStateUrl + "/api/room/state";
  const std::string body = message.dump();
  const std::string room = fRoomId;

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
    try {
      const NHttpResponse response = http.request("POST", url, body, headers);
      if (response.status < 200 || response.status >= 300) {
        NLogWarning("Cannot report the session of room '%s' (HTTP %d)", room.c_str(), response.status);
        return;
      }
    }
    catch (const std::exception & e) {
      NLogWarning("Cannot report the session of room '%s': %s", room.c_str(), e.what());
      return;
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

  NHttpResponse response;
  bool          fetched    = false;
  std::string   fetchError;
  for (int attempt = 1; attempt <= kRoomRestoreFetchAttempts && !fetched; ++attempt) {
    NHttpRequest http;
    http.SetTimeout(kRoomRouterConnectMs, kRoomRouterReadMs);
    try {
      response = http.request("POST", fRoomStateUrl + "/api/room/state", request.dump(), headers);
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
  // exactly as they do for a client - the route NMcpServer::CallTool already takes.
  const NRoomSession::Dispatch dispatch = [this](const std::string & method, const std::string & route,
                                                 const json & body, std::string & dispatchError) -> json {
    auto arg = std::make_shared<THttpCallArg>();
    arg->SetMethod(method.c_str());
    arg->SetPathName("api");
    arg->SetFileName(route.c_str());
    arg->SetPostData(body.dump().c_str());

    ProcessRequest(arg);

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
  Dispatch(std::move(arg), nullptr);
}

void NHttpServer::ProcessRequestAs(std::shared_ptr<THttpCallArg> arg, const NRequestIdentity & identity)
{
  Dispatch(std::move(arg), &identity);
}

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
  const bool isWsBridged = arg->GetWSId() != 0;
  if (fOidcVerifier && !isWsBridged && !fullpath.IsNull() && fullpath != "openapi/inspector" &&
      fullpath != "inspector/openapi") {
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
  if (!ApplyRoomAccess(arg.get(), method.Data(), /*isPage=*/false)) return;

  json out;
  json wsOut;
  if (fullpath.IsNull()) {

    out["result"]  = "success";
    out["message"] = "Welcome to NHttpServer API";
    // out["ws"]["path"] = "ws/root.websocket";

    out["state"]["history"]   = GetJson();
    out["state"]["users"]     = fNWsHandler ? fNWsHandler->GetClientCount() : 0;
    out["state"]["workspace"] = GetWorkspace();

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
      // Roll back any existing entry for this route (and every newer entry)
      // before running the handler. Their DELETE handlers delete the stale
      // in-memory objects and close the underlying files. Doing this first
      // prevents those DELETE handlers from tearing down the objects this
      // request is about to create under the same keys.
      if (fUseHistory && !method.CompareTo("POST")) {
        fWorkspace.RemoveEntry(fullpath.Data());
      }

      handlerFn(method.Data(), in, out, wsOut, fObjectsMap);
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
      TString hdr = arg->GetRequestHeader("X-NDMSPC-Suppress-Workspace-Publish");
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
      NLogDebug("Suppressing workspace keys from broadcast due to X-NDMSPC-Suppress-Workspace-Publish header");
    }

    if (!wsOut["payload"].is_null() || !wsOut["workspace"].is_null() || !wsOut["state"].is_null()) {
      json wsMessage;
      wsMessage["event"]   = "ngnt";
      wsMessage["payload"] = wsOut["payload"].is_null() ? json::object() : wsOut["payload"];
      // If state is present, include it in the same payload
      if (!wsOut["state"].is_null()) {
        wsMessage["payload"]["state"] = wsOut["state"];
      }

      // loop over keys in wsOut["workspace"] and add them to workspace, overwriting existing ones if necessary
      if (!wsOut["workspace"].is_null()) {
        // Build workspace with order of keys same as in NHistoryEntry
        json workspace;
        for (const auto & entry : fWorkspace.GetEntries()) {
          // History entries use full path (e.g. "ngnt/open"), but workspace uses short keys ("open")
          std::string entryName = entry->GetName();
          std::string wsKey     = entryName;
          if (!fGroup.empty() && entryName.rfind(fGroup + "/", 0) == 0) {
            wsKey = entryName.substr(fGroup.size() + 1);
          }
          NLogTrace("Adding workspace entry for: %s (wsKey: %s)", entryName.c_str(), wsKey.c_str());
          // skip suppressed keys
          if (suppressedWorkspaceKeys.find(wsKey) != suppressedWorkspaceKeys.end()) {
            NLogTrace("Skipping suppressed workspace entry for: %s", wsKey.c_str());
            continue;
          }
          // Avoid using non-const operator[] on GetWorkspace() as it would
          // insert a null value for missing keys. Use contains()+at() instead.
          {
            const json & srvWs = GetWorkspace();
            if (srvWs.contains(wsKey) && !srvWs.at(wsKey).is_null()) {
              workspace[wsKey] = srvWs.at(wsKey);
            }
          }
        }

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
          GetWorkspace()[it.key()]     = it.value();
        }
        wsMessage["payload"]["workspace"]["schema"]["properties"] = workspace;

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
