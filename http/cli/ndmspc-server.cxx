#include <algorithm>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "TROOT.h"
#include <TSystem.h>
#include <TFile.h>
#include <TApplication.h>
#include <CLI/CLI.hpp>
#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NBaseActions.h"
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NMcpServer.h"
#include "ndmspc/http/NRoomRouter.h"
#include "ndmspc/http/NX509Authenticator.h"
#include "ndmspc/http/NX509Config.h"

#include "ndmspc/ndmspc.h"

namespace {

/// How a tool macro is reached before it is loaded: the action prefixes that tell us a request needs
/// the macro, and the entry action published as a placeholder so the tool group is still visible in
/// `/api` before the macro runs. A macro not listed here is still deferred; it just has no
/// placeholder, so the resolver loads it when the first action it serves is requested.
struct ToolRouting {
  std::vector<std::string> prefixes;
  std::string              entryAction;
};

const std::map<std::string, ToolRouting> kToolRouting = {
    {"toolNgnt.C", {{"ndmspc/ngnt/"}, "ndmspc/ngnt/open"}},
    {"toolBrowser.C", {{"ndmspc/browser/", "ndmspc/rbrowser/"}, "ndmspc/browser/open"}},
    {"toolSlurm.C", {{"ndmspc/slurm/"}, "ndmspc/slurm/submit"}},
};

/// A tool macro left out of startup, made concrete once the macro list is resolved.
struct DeferredMacro {
  std::string              path;
  std::vector<std::string> prefixes;
  std::string              entryAction;
};

std::vector<DeferredMacro> gDeferredMacros;
std::mutex                 gDeferredMutex;

/// @brief Whether a macro declares that it serves `action`.
bool ServesAction(const DeferredMacro & macro, const std::string & action)
{
  return std::any_of(macro.prefixes.begin(), macro.prefixes.end(),
                     [&action](const std::string & prefix) { return action.rfind(prefix, 0) == 0; });
}

/// @brief Load one deferred macro and install the handlers it registered.
/// @param it The macro to load; erased on success.
/// @param reason The request that needed it, for the log line.
/// @return False when the macro failed to load (it stays deferred, so a later request retries).
bool LoadDeferredMacroAt(std::vector<DeferredMacro>::iterator it, const std::string & reason)
{
  const auto start = std::chrono::steady_clock::now();
  NLogInfo("Deferred macro '%s' is needed by %s; loading ...", it->path.c_str(), reason.c_str());
  if (!Ndmspc::NUtils::ExecMacro(it->path)) {
    NLogError("Deferred macro '%s' failed to load; it stays deferred", it->path.c_str());
    return false;
  }
  if (Ndmspc::gNHttpServer != nullptr && Ndmspc::gNdmspcHttpHandlers != nullptr) {
    Ndmspc::gNHttpServer->SetHttpHandlers(*Ndmspc::gNdmspcHttpHandlers);
  }
  const double elapsedMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  NLogInfo("Deferred macro '%s' loaded in %.0f ms", it->path.c_str(), elapsedMs);
  gDeferredMacros.erase(it);
  return true;
}

/// Load the deferred macro that serves `action`, if there is one. Runs on the action worker (the
/// server resolves an action before classifying it), so a macro load never overlaps another action.
/// Installs the new handlers in the server before returning.
bool LoadDeferredMacro(const std::string & action)
{
  std::lock_guard<std::mutex> lock(gDeferredMutex);

  // A declared prefix says exactly which macro serves the action.
  for (auto it = gDeferredMacros.begin(); it != gDeferredMacros.end(); ++it) {
    if (!ServesAction(*it, action)) continue;
    return LoadDeferredMacroAt(it, "'" + action + "'");
  }

  // The action is already served: a request to a loaded tool must not pull in the macros that were
  // never given a routing hint just because they are still pending.
  if (Ndmspc::gNHttpServer != nullptr && Ndmspc::gNHttpServer->FindHttpHandler(action) != nullptr) {
    return false;
  }

  // No declared prefix: a macro we have no routing for. Load the pending ones, one at a time, until
  // one registers the action. A miss therefore costs a load per such macro - the price of deferring
  // a macro whose actions nobody declared in advance.
  std::size_t i = 0;
  while (i < gDeferredMacros.size()) {
    if (!gDeferredMacros[i].prefixes.empty()) {
      ++i;
      continue;
    }
    if (!LoadDeferredMacroAt(gDeferredMacros.begin() + i, "'" + action + "'")) return false;
    if (Ndmspc::gNHttpServer == nullptr) return true;
    if (Ndmspc::gNHttpServer->FindHttpHandler(action) != nullptr) return true;
    // It loaded but does not serve this action; it is gone, so `i` now names the next one.
  }
  return false;
}

/// The placeholder at a deferred tool's entry action. The server resolves the action before this is
/// reached, so it is a fallback for a macro that failed to load: it loads the action it was called
/// for and runs the real handler. The rest of that tool's actions go through the resolver.
void LazyToolEntry(std::string method, json & in, json & out, json & wsOut,
                   std::map<std::string, TObject *> & objects)
{
  const std::string action =
      Ndmspc::gNHttpServer != nullptr ? Ndmspc::gNHttpServer->GetCurrentAction() : std::string();
  if (action.empty() || !LoadDeferredMacro(action)) {
    out["result"] = "failure";
    out["code"]   = "tool_unavailable";
    out["error"]  = action.empty() ? "The deferred tool is no longer pending"
                                   : "The deferred tool could not be loaded";
    return;
  }
  Ndmspc::NHttpFuncPtr real =
      Ndmspc::gNHttpServer != nullptr ? Ndmspc::gNHttpServer->FindHttpHandler(action) : nullptr;
  if (real == nullptr || real == &LazyToolEntry) {
    out["result"] = "failure";
    out["code"]   = "tool_unavailable";
    out["error"]  = "The deferred tool registered no handler for '" + action + "'";
    return;
  }
  real(method, in, out, wsOut, objects);
}

/// Load every deferred macro before an MCP tool list is built. This is what keeps a deferred tool
/// discoverable: the client asks what it can call, the tool loads here, and its metadata is in the
/// list. It runs at most until the list is empty; later calls are cheap.
bool LoadAllDeferredMacros()
{
  std::lock_guard<std::mutex> lock(gDeferredMutex);

  while (!gDeferredMacros.empty()) {
    const DeferredMacro macro  = gDeferredMacros.front();
    const auto           start = std::chrono::steady_clock::now();
    NLogInfo("Deferred macro '%s' is needed by the tool list; loading ...", macro.path.c_str());
    if (!Ndmspc::NUtils::ExecMacro(macro.path)) {
      NLogError("Deferred macro '%s' failed to load; it stays deferred", macro.path.c_str());
      return false;
    }
    if (Ndmspc::gNHttpServer != nullptr && Ndmspc::gNdmspcHttpHandlers != nullptr) {
      Ndmspc::gNHttpServer->SetHttpHandlers(*Ndmspc::gNdmspcHttpHandlers);
    }
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    NLogInfo("Deferred macro '%s' loaded in %.0f ms", macro.path.c_str(), elapsedMs);
    gDeferredMacros.erase(gDeferredMacros.begin());
  }
  return true;
}

} // namespace

static inline void EnsureServerRunning(Ndmspc::NHttpServer * serv, int port)
{
  if (!serv || !serv->IsAnyEngine()) {
    NLogError("Server is not running on port %d: address may be in use.\n"
              "Hints:\n"
              " - Check for a process listening on the port: `lsof -i :%d` or `ss -ltnp | grep :%d`.\n"
              " - Check other terminals or system services (systemd) that may have started the server.\n"
              " - Check for Docker containers exposing the port: `docker ps --format '{{.ID}} {{.Names}} {{.Ports}}' | "
              "grep :%d`.\n"
              "If this is unexpected, stop the conflicting process or choose a different port.\n",
              port, port, port, port);
    exit(1);
  }
}

std::string app_description()
{
  size_t size = 64;
  auto   buf  = std::make_unique<char[]>(size);
  // char   buf[size];
  size = std::snprintf(buf.get(), size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

std::string app_version()
{
  size_t size = 128;
  auto   buf  = std::make_unique<char[]>(size);
  size        = std::snprintf(buf.get(), size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

void log_server_version(int port)
{
  NLogInfo("Starting ndmspc http server on port %d with %s", port, app_version().c_str());
}

void handle_sigterm(int sig)
{
  std::cout << ">>> SIGNAL RECEIVED: " << sig << " <<<" << std::endl;
  gSystem->Exit(0);
}

std::string EnvString(const char * name)
{
  const char * value = std::getenv(name);
  return value ? value : "";
}

long EnvLong(const char * name, long defaultValue)
{
  const auto value = EnvString(name);
  if (value.empty()) return defaultValue;
  size_t consumed = 0;
  const auto parsed = std::stol(value, &consumed);
  if (consumed != value.size()) throw std::invalid_argument(std::string(name) + " must be an integer");
  return parsed;
}

Ndmspc::NOidcConfig OidcConfigFromEnvironment()
{
  Ndmspc::NOidcConfig config;
  config.issuer = EnvString("NDMSPC_OIDC_ISSUER");
  config.audience = EnvString("NDMSPC_OIDC_AUDIENCE");
  config.caFile = EnvString("NDMSPC_OIDC_CA_FILE");
  config.caPath = EnvString("NDMSPC_OIDC_CA_PATH");
  config.clockSkew = std::chrono::seconds(EnvLong("NDMSPC_OIDC_CLOCK_SKEW_SECONDS", 30));
  config.jwksRefresh = std::chrono::seconds(EnvLong("NDMSPC_OIDC_JWKS_REFRESH_SECONDS", 300));
  config.jwksMaxStale = std::chrono::seconds(EnvLong("NDMSPC_OIDC_JWKS_MAX_STALE_SECONDS", 86400));
  config.authenticationTimeout = std::chrono::seconds(EnvLong("NDMSPC_OIDC_AUTH_TIMEOUT_SECONDS", 15));
  config.httpTimeout = std::chrono::milliseconds(EnvLong("NDMSPC_OIDC_HTTP_TIMEOUT_MS", 5000));
  config.allowInsecureHttp = Ndmspc::NUtils::ParseBoolEnv(std::getenv("NDMSPC_OIDC_ALLOW_INSECURE_HTTP"));
  return config;
}

Ndmspc::NX509Config X509ConfigFromEnvironment()
{
  Ndmspc::NX509Config config;
  config.certFile = EnvString("NDMSPC_X509_CERT");
  config.keyFile = EnvString("NDMSPC_X509_KEY");
  config.caFile = EnvString("NDMSPC_X509_CA_FILE");
  config.caPath = EnvString("NDMSPC_X509_CA_PATH");
  config.verifyOptional =
      Ndmspc::NUtils::ParseBoolEnv(std::getenv("NDMSPC_X509_VERIFY_OPTIONAL"));
  const std::string identity = EnvString("NDMSPC_X509_IDENTITY");
  if (!identity.empty()) config.identity = identity;
  config.internalPort = static_cast<int>(EnvLong("NDMSPC_X509_INTERNAL_PORT", 8081));
  config.cors = EnvString("NDMSPC_X509_CORS");
  return config;
}

void AddOidcOptions(CLI::App * command, Ndmspc::NOidcConfig & config)
{
  command->add_option("--oidc-issuer", config.issuer, "OIDC issuer URL");
  command->add_option("--oidc-audience", config.audience, "Required OIDC token audience");
  command->add_option("--oidc-ca-file", config.caFile, "OIDC TLS CA certificate file");
  command->add_option("--oidc-ca-path", config.caPath, "OIDC TLS CA directory (hashed certificates)");
  command->add_option_function<long>("--oidc-clock-skew", [&config](long value) { config.clockSkew = std::chrono::seconds(value); }, "OIDC clock skew in seconds");
  command->add_option_function<long>("--oidc-jwks-refresh", [&config](long value) { config.jwksRefresh = std::chrono::seconds(value); }, "JWKS refresh interval in seconds");
  command->add_option_function<long>("--oidc-jwks-max-stale", [&config](long value) { config.jwksMaxStale = std::chrono::seconds(value); }, "Maximum JWKS staleness in seconds");
  command->add_option_function<long>("--oidc-auth-timeout", [&config](long value) { config.authenticationTimeout = std::chrono::seconds(value); }, "WebSocket authentication timeout in seconds");
  command->add_option_function<long>("--oidc-http-timeout", [&config](long value) { config.httpTimeout = std::chrono::milliseconds(value); }, "OIDC HTTP timeout in milliseconds");
  command->add_flag("--oidc-allow-insecure-http", config.allowInsecureHttp, "Allow HTTP issuer for development");
}

void AddX509Options(CLI::App * command, Ndmspc::NX509Config & config)
{
  command->add_option("--x509-cert", config.certFile, "X509 server certificate (PEM)");
  command->add_option("--x509-key", config.keyFile, "X509 server private key (PEM)");
  command->add_option("--x509-ca-file", config.caFile, "X509 CA bundle for verifying client certificates");
  command->add_option("--x509-ca-path", config.caPath, "X509 CA directory (hashed certificates) for verifying client certificates");
  command->add_flag("--x509-verify-optional", config.verifyOptional, "Do not require a client certificate (verify when presented)");
  command->add_option("--x509-identity", config.identity, "Identity attribute extracted from the client cert subject: 'cn' (default) or 'dn'");
  command->add_option("--x509-internal-port", config.internalPort, "Loopback port for the internal ROOT engine (default: 8081)");
  command->add_option("--x509-cors", config.cors, "Allowed CORS origins for browser clients ('*' or a comma-separated list; empty disables CORS)");
}

void PrepareOidcConfig(Ndmspc::NOidcConfig & config, const Ndmspc::NX509Config & x509Config)
{
  config.Normalize();
  config.Validate();
  x509Config.Validate();
  // Mutual TLS (X509) and OIDC are mutually exclusive authentication modes.
  if (config.Enabled() && x509Config.Enabled()) {
    throw std::invalid_argument("OIDC and X509 authentication are mutually exclusive; configure only one");
  }
  if (config.Enabled()) NLogInfo("OIDC authentication enabled for issuer '%s' and audience '%s'", config.issuer.c_str(), config.audience.c_str());
  if (x509Config.Enabled()) NLogInfo("X509 (mutual TLS) authentication enabled with identity from '%s'", x509Config.identity.c_str());
}

int main(int argc, char ** argv)
{

  // Or standard C:
  signal(SIGTERM, handle_sigterm);

  gROOT->SetBatch(kTRUE);
  TApplication rootApp("myapp", 0, nullptr);
  // if (getenv("NDMSPC_CACHE")) {
  //   std::string              cache               = getenv("NDMSPC_CACHE");
  //   std::vector<std::string> cacheOpts           = Ndmspc::NUtils::Tokenize(cache.c_str(), ':');
  //   std::string              cacheDir            = cacheOpts[0];
  //   int                      operateDisconnected = atoi(cacheOpts[1].c_str());
  //   int                      forceCacheRead      = atoi(cacheOpts[2].c_str());
  //   TFile::SetCacheFileDir(gSystem->ExpandPathName(cacheDir.c_str()), operateDisconnected, forceCacheRead);
  // }

  int port = 8080;
  if (gSystem->Getenv("PORT")) {
    port = atoi(gSystem->Getenv("PORT"));
  }
  bool     batch = true;
  auto oidcConfig = OidcConfigFromEnvironment();
  auto x509Config = X509ConfigFromEnvironment();
  CLI::App app{app_description()};
  app.set_version_flag("--version", app_version(), "Print version information and exit");
  argv = app.ensure_utf8(argv);

  app.add_option("-p,--port", port, "Server port (default: 8080)");
  // How many connections the HTTP engine serves at once, including the websockets - each of which holds
  // one for as long as it is open. This is therefore the real ceiling on concurrent clients, and the
  // engine's own default is 10: a router whose threads are all held by watchers starts refusing
  // connections, and a refused client waits for a thread that never frees (the page looks frozen, and a
  // plain /api request with it). 128 leaves headroom under a room's own connection ceiling
  // (NDMSPC_ROOM_MAX_CONNECTIONS, 100 by default).
  int httpThreads = 128;
  if (const char * threadsEnv = std::getenv("NDMSPC_HTTP_THREADS"); threadsEnv != nullptr && *threadsEnv != '\0') {
    const int parsed = std::atoi(threadsEnv);
    if (parsed > 0) httpThreads = parsed;
  }
  app.add_option("--http-threads", httpThreads,
                 "Connections served at once, websockets included (default: 128; "
                 "NDMSPC_HTTP_THREADS overrides)")
      ->default_val(httpThreads);
  std::string macroFilename;
  app.add_option("-m,--macro", macroFilename,
                 "Macro path list separated by commas (default: auto-load "
                 "$NDMSPC_DIR/macros/tools/toolNgnt.C,toolBrowser.C; the base actions are built in). A "
                 "name with no "
                 "directory of its own takes the directory of the entry before it — "
                 "<dir>/one.C,two.C loads both from <dir> — and a list that begins with a bare name "
                 "falls back on $NDMSPC_DIR/macros/tools. Ignored with --rooms, which serves rooms "
                 "only");
  app.add_option("-b,--batch", batch, "Batch mode without graphics (default: true)");
  std::string htmlDir = "";
  app.add_option("--html", htmlDir, "Directory with static assets (default: empty, use built-in)");
  bool noHistory = false;
  app.add_option("--no-history", noHistory, "Disable history in processing requests")->default_val("false");
  int heartbeat_ms = 10000;
  app.add_option("--heartbeat", heartbeat_ms, "Heartbeat interval in milliseconds (default: 10000)");
  bool withMcp = true;
  if (const char * mcpEnv = std::getenv("NDMSPC_MCP"); mcpEnv != nullptr && *mcpEnv != '\0') {
    withMcp = Ndmspc::NUtils::ParseBoolEnv(mcpEnv);
  }
  app.add_option("--mcp", withMcp,
                 "Expose the MCP endpoint (POST /api/mcp); enabled by default (--mcp false or "
                 "NDMSPC_MCP=0 disables)")
      ->default_val(withMcp ? "true" : "false");
  // WebSocket support: on by default, and worth turning off where no client uses the socket -
  // the room router above all, whose clients connect to a room, never to the router itself.
  bool withWs = true;
  if (const char * wsEnv = std::getenv("NDMSPC_WS"); wsEnv != nullptr && *wsEnv != '\0') {
    withWs = Ndmspc::NUtils::ParseBoolEnv(wsEnv);
  }
  app.add_option("--ws", withWs,
                 "Serve the WebSocket endpoint (/ws/root.websocket); enabled by default "
                 "(--ws false or NDMSPC_WS=0 serves no websocket at all)")
      ->default_val(withWs ? "true" : "false");
  // Room router: off unless asked for. It registers the framework's NRoomRouter, which serves
  // /api/ndmspc/room/* and creates one Knative Service per room.
  bool withRooms = false;
  if (const char * roomsEnv = std::getenv("NDMSPC_ROOMS"); roomsEnv != nullptr && *roomsEnv != '\0') {
    withRooms = Ndmspc::NUtils::ParseBoolEnv(roomsEnv);
  }
  app.add_option("--rooms", withRooms,
                 "Also serve the room router (NRoomRouter): /api/ndmspc/room/*, one Knative Service "
                 "per room, and the room websocket policy; disabled by default (--rooms true "
                 "or NDMSPC_ROOMS=1). A router serves rooms only, so no macro is loaded. "
                 "Kubernetes only: the server exits at startup when KUBERNETES_SERVICE_HOST is unset")
      ->default_val(withRooms ? "true" : "false");
  // Build-time (or dev) snapshot of the MCP tool list: load every macro once, write what a client
  // would see, and exit. The runtime serves that file instead of loading macros for `tools/list`
  // (see NMcpServer::BuildTools); with no file it loads them as before.
  std::string catalogPath;
  app.add_option("--write-tool-catalog", catalogPath,
                 "Write the MCP tool catalog to this file and exit (used by the image build; a "
                 "developer can also point NDMSPC_TOOL_CATALOG at the result)");
  AddOidcOptions(&app, oidcConfig);
  AddX509Options(&app, x509Config);

  app.callback([&rootApp, &port, &macroFilename, &batch, &htmlDir, &noHistory, &heartbeat_ms, &withMcp,
                &withRooms, &withWs, &oidcConfig, &x509Config, &httpThreads, &catalogPath]() {
    gROOT->SetBatch(batch);
    PrepareOidcConfig(oidcConfig, x509Config);

    // Rooms need the in-cluster API: refuse before the server is built and the macros are loaded,
    // so --rooms outside Kubernetes fails at startup with a clear error instead of later - or not
    // at all, should the startup fail for another reason first. A process that refuses to start must
    // say why, so this goes out even when console logging is turned off (NDMSPC_LOG_CONSOLE).
    if (withRooms) {
      std::string reason;
      if (!Ndmspc::NRoomRouter::KubernetesAvailable(&reason)) {
        NLogForce("[ERROR] [room] %s", reason.c_str());
        exit(1);
      }
    }

    Ndmspc::NHttpServer * serv =
        new Ndmspc::NHttpServer("", withWs, heartbeat_ms, oidcConfig, false);
    if (!withWs) {
      NLogInfo("WebSocket support is disabled (--ws false): there is no /ws/root.websocket endpoint");
    }
    if (serv == nullptr) {
      NLogError("Server was not created !!!");
      exit(1);
    }
    log_server_version(port);

    serv->SetUseHistory(!noHistory);
    serv->SetMcpEnabled(withMcp);
    if (withMcp) NLogInfo("MCP endpoint enabled (POST /api/mcp)");
    serv->SetCors("*");
    if (htmlDir.empty()) {
      const char * env       = gSystem->Getenv("NDMSPC_DIR");
      std::string  ndmspcDir = (env && *env) ? env : "/usr";
      htmlDir                = TString::Format("%s/share/ndmspc/ndmspc-ui", ndmspcDir.c_str()).Data();
    }

    if (!htmlDir.empty() && gSystem->AccessPathName(htmlDir.c_str()) == 0) {
      NLogInfo("Using '%s' as directory with static assets.", htmlDir.c_str());
      serv->AddLocation("assets/", TString::Format("%s/assets", htmlDir.c_str()).Data());
      serv->SetDefaultPage(TString::Format("%s/index.html", htmlDir.c_str()).Data());
    }

    // The directory holding the installed macros, for the default -m list below. A server started
    // with rooms loads no macro at all (the room router serves /api/ndmspc/room/* only), so it is resolved
    // - and the defaults applied - only when this is not a router.
    const char * envMacros = gSystem->Getenv("NDMSPC_DIR");
    std::string  ndmspcMacrosDir = (envMacros && *envMacros) ? envMacros : "";
    if (ndmspcMacrosDir.empty()) {
      const char * envHome = gSystem->Getenv("NDMSPC__HOME");
      ndmspcMacrosDir      = (envHome && *envHome) ? envHome : "/usr/share/ndmspc";
    }

    if (!withRooms && macroFilename.empty()) {
      // check if ndmspcMacrosDir is exists
      if (gSystem->AccessPathName(ndmspcMacrosDir.c_str()) == 0) {
        // The base actions (health, state) are built into the server, so only the tools a deployment
        // actually wants have to be named here. The default is the analysis tools and the ROOT file
        // browser; the second is bare, so it takes the directory the first names.
        macroFilename =
            TString::Format("%s/macros/tools/toolNgnt.C,toolBrowser.C", ndmspcMacrosDir.c_str()).Data();
        NLogInfo("No macro file given, using default macros ...");
      } else {
        // just warn and continue, user may provide macro file later
        NLogError("No macro file given and default macros not found in '%s'. Please provide a macro file with -m option.", ndmspcMacrosDir.c_str());
        exit(1);
      }
    }

    NLogInfo("NDMSPC server heartbeat: %d ms", heartbeat_ms);

    // Your local map
    std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
    // Set the global pointer to your local map
    Ndmspc::gNdmspcHttpHandlers = &handlers;
    // MCP tool metadata declared by the macros (descriptions, allowed verbs, ...)
    Ndmspc::NMcpToolMap mcpTools;
    Ndmspc::gNdmspcMcpTools = &mcpTools;

    // The server's own base actions (health, state) are framework code, not a macro: register them
    // here so they exist whatever -m says. A room router serves /api/ndmspc/room/* only, so it gets none.
    if (!withRooms) Ndmspc::RegisterBaseActions();

    // A router serves rooms, not tools, so it loads no macro at all - not even one named on the
    // command line (a deployment's image CMD passes -m .../toolNgnt.C, which must not reach the
    // entry: the room router is rooms and nothing else).
    std::vector<std::string> macros;
    if (withRooms) {
      if (!macroFilename.empty()) {
        NLogWarning("--rooms: ignoring the macro list '%s'; the room router serves only /api/ndmspc/room/*",
                    macroFilename.c_str());
      }
      NLogInfo("Rooms enabled: serving the room actions only (no tool macro is loaded)");
    }
    else {
      macros = Ndmspc::NUtils::ResolveMacroList(macroFilename, ndmspcMacrosDir + "/macros/tools");

      // The catalog generator describes every installed tool, so it is the one caller that looks past
      // -m. A normal server keeps exactly the list it was given: naming two macros must not defer the
      // others as well.
      if (!catalogPath.empty()) {
        const std::string dir = ndmspcMacrosDir + "/macros/tools";
        if (void * dirp = gSystem->OpenDirectory(dir.c_str())) {
          while (const char * entry = gSystem->GetDirEntry(dirp)) {
            const std::string name(entry);
            if (name.size() <= 6 || name.compare(0, 4, "tool") != 0 ||
                name.compare(name.size() - 2, 2, ".C") != 0) {
              continue;
            }
            const std::string path = dir + "/" + name;
            if (std::find(macros.begin(), macros.end(), path) == macros.end()) macros.push_back(path);
          }
          gSystem->FreeDirectory(dirp);
        }
      }

      // Nothing but the server's own base actions is loaded at startup. NDMSPC_PRELOAD_TOOLS names
      // the macros to load eagerly: comma-separated base names, with or without `.C`, and `all`/`*`
      // for every one. NDMSPC_DEFER_TOOLS=0 is kept as the old way of asking for them all.
      bool                  preloadAll = false;
      std::set<std::string> preload;
      if (const char * preloadEnv = gSystem->Getenv("NDMSPC_PRELOAD_TOOLS")) {
        const std::string list(preloadEnv);
        std::size_t       pos = 0;
        while (pos <= list.size()) {
          const std::size_t comma = list.find(',', pos);
          std::string       item  = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
          const auto        first = item.find_first_not_of(" \t\r\n");
          const auto        last  = item.find_last_not_of(" \t\r\n");
          item = first == std::string::npos ? std::string() : item.substr(first, last - first + 1);
          if (!item.empty()) {
            if (item == "*" || item == "all") {
              preloadAll = true;
            }
            else {
              if (item.size() > 2 && item.compare(item.size() - 2, 2, ".C") == 0) item.resize(item.size() - 2);
              preload.insert(item);
            }
          }
          if (comma == std::string::npos) break;
          pos = comma + 1;
        }
      }
      if (const char * deferEnv = gSystem->Getenv("NDMSPC_DEFER_TOOLS")) {
        const std::string value(deferEnv);
        if (value == "0" || value == "false" || value == "no" || value == "off") preloadAll = true;
      }

      NLogInfo("Going to load %d macro(s). Waiting ...", static_cast<int>(macros.size()));
      gDeferredMacros.clear();
      const auto  macrosStart = std::chrono::steady_clock::now();
      std::size_t loaded      = 0;
      std::size_t deferred    = 0;
      for (const auto & macro : macros) {
        const std::string name = gSystem->BaseName(macro.c_str());
        const std::string stem = (name.size() > 2 && name.compare(name.size() - 2, 2, ".C") == 0)
                                     ? name.substr(0, name.size() - 2)
                                     : name;
        const auto        routing = kToolRouting.find(name);

        // Everything is deferred unless the environment preloads it. The bundled tools name their
        // actions, so they get an entry placeholder (and their group stays visible in `/api`); a
        // macro we know nothing about is deferred without one and found by the resolver on use.
        if (!preloadAll && preload.count(name) == 0 && preload.count(stem) == 0) {
          // A list can name a macro the image no longer ships (a tool that moved away): deferring it
          // would publish a group with no tools and fail only when somebody used it, so say so and
          // leave it out. Remote macros are not checked - they are fetched when they load.
          if (macro.find("://") == std::string::npos && gSystem->AccessPathName(macro.c_str())) {
            NLogWarning("Macro '%s' does not exist; skipping it", macro.c_str());
            continue;
          }
          const bool alreadyDeferred =
              std::any_of(gDeferredMacros.begin(), gDeferredMacros.end(),
                          [&macro](const DeferredMacro & entry) { return entry.path == macro; });
          if (!alreadyDeferred) {
            DeferredMacro entry;
            entry.path = macro;
            if (routing != kToolRouting.end()) {
              entry.prefixes    = routing->second.prefixes;
              entry.entryAction = routing->second.entryAction;
            }
            gDeferredMacros.push_back(std::move(entry));
            ++deferred;
            NLogInfo("Deferring macro '%s' until a request needs it", macro.c_str());
          }
          continue;
        }

        const auto macroStart = std::chrono::steady_clock::now();
        // ExecMacro uses the image's precompiled library when one matches this macro and ROOT, and
        // interprets it otherwise. A macro that cannot be opened at all still fails startup, as before.
        if (!Ndmspc::NUtils::ExecMacro(macro)) {
          NLogError("Cannot load macro '%s'. Check the path, or give the macro to load with -m.", macro.c_str());
          exit(1);
        }
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - macroStart).count();
        NLogInfo("Macro '%s' loaded in %.0f ms", macro.c_str(), elapsedMs);
        ++loaded;
      }

      const double totalMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - macrosStart).count();
      NLogInfo("Tool macros: %zu loaded in %.0f ms, %zu deferred", loaded, totalMs, deferred);
      for (const auto & deferredMacro : gDeferredMacros) {
        if (!deferredMacro.entryAction.empty()) handlers[deferredMacro.entryAction] = &LazyToolEntry;
      }
      if (!gDeferredMacros.empty()) {
        serv->SetActionResolver(&LoadDeferredMacro);
        Ndmspc::gNdmspcToolLoader = &LoadAllDeferredMacros;
      }
    }

    if (!Ndmspc::gNdmspcHttpHandlers) {
      return;
    }

    if (withRooms) {
      // The room router is framework code (NRoomRouter), not a macro: register its actions before
      // the handler map is handed to the server, so nothing races with the engine start below.
      if (!Ndmspc::NRoomRouter::Instance().Register(serv)) exit(1); // it logged why
      const Ndmspc::NRoomConfig roomCfg = Ndmspc::NRoomConfig::FromEnv();
      NLogInfo("Rooms enabled: serving /api/ndmspc/room/* from the room router in namespace '%s' "
               "(one Knative Service per room, named '%s<id>')",
               roomCfg.ns.c_str(), roomCfg.prefix.c_str());
    }

    serv->SetHttpHandlers(handlers);

    // A build step can ask for the tool catalog and leave: every macro is loaded here, the list is
    // built once, and the runtime serves it without loading a macro (see NMcpServer::BuildTools).
    if (!catalogPath.empty()) {
      LoadAllDeferredMacros();
      Ndmspc::NMcpServer mcp(serv);
      std::ofstream       out(catalogPath);
      if (!out) {
        NLogError("Cannot write the tool catalog to '%s'", catalogPath.c_str());
        exit(1);
      }
      const json catalog = mcp.BuildTools();
      out << catalog.dump(2) << "\n";
      NLogInfo("Wrote the tool catalog to %s", catalogPath.c_str());
      return;
    }

    // All handlers are registered now: start the HTTP engine. Nothing has been
    // listening up to this point, so no request could have raced with the
    // handler-map population above.
    if (x509Config.Enabled()) {
      // X509 (mutual TLS) mode: the ROOT engine stays private on loopback with
      // OIDC disabled; the httplib front-door on the public port terminates TLS,
      // verifies client certificates, extracts the subject as the username, and
      // forwards HTTP + WebSocket traffic here.
      const std::string internalBase = TString::Format("http://127.0.0.1:%d", x509Config.internalPort).Data();
      serv->StartEngine(TString::Format("http:127.0.0.1:%d?top=ndmspc&thrds=%d", x509Config.internalPort,
                                        httpThreads)
                            .Data());
      EnsureServerRunning(serv, x509Config.internalPort);
      if (serv->IsTerminated()) {
        NLogError("Server is zombie, exiting ...");
        exit(1);
      }
      NLogInfo("Internal ROOT engine listening on loopback port %d (%d connection threads)", x509Config.internalPort,
               httpThreads);

      Ndmspc::NX509Authenticator frontDoor(x509Config);
      // The front door verifies the client certificate itself and forwards what it found in the
      // request headers; the engine it forwards to is loopback-only, so nothing else can write them.
      serv->SetTrustForwardedIdentity(true);
      if (!frontDoor.Start("0.0.0.0", port, internalBase)) {
        NLogError("Failed to start the X509 (mutual TLS) front-door on port %d", port);
        exit(1);
      }
      NLogInfo("X509 server is running and ready to use on port %d ...", port);

      int timeout = 100;
      while (!gSystem->ProcessEvents()) {
        gSystem->Sleep(timeout);
      }
      serv->SetReadOnly(kFALSE);
      serv->Print();
      rootApp.Run();
      return;
    }

    // Said out loud, because the limit is otherwise invisible until it is hit: a websocket holds one of
    // these for as long as it is open, and a client that cannot get one waits rather than failing.
    NLogInfo("HTTP engine: serving up to %d connections at once, websockets included (--http-threads)",
             httpThreads);
    serv->StartEngine(TString::Format("http:%d?top=ndmspc&thrds=%d", port, httpThreads).Data());
    EnsureServerRunning(serv, port);

    if (serv->IsTerminated()) {
      NLogError("Server is zombie, exiting ...");
      exit(1);
    }

    NLogInfo("Server is running and ready to use on port %d ...", port);

    int timeout = 100;
    while (!gSystem->ProcessEvents()) {
      gSystem->Sleep(timeout);
    }

    // when read-only mode disabled one could execute object methods like TTree::Draw()
    serv->SetReadOnly(kFALSE);
    serv->Print();
    rootApp.Run();
  });

  try {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError & e) {
    return app.exit(e);
  }
  catch (const std::exception & e) {
    NLogError("Server startup failed: %s", e.what());
    if (oidcConfig.Enabled()) {
      NLogError("Cannot initialize OIDC authentication. Verify that Keycloak is running, the issuer URL is reachable, and the realm exists.");
      NLogError("Expected discovery document: %s/.well-known/openid-configuration", oidcConfig.issuer.c_str());
    }
    return 1;
  }

  return 0;
}
