#include <csignal>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include "TROOT.h"
#include <TSystem.h>
#include <TFile.h>
#include <TApplication.h>
#include <CLI/CLI.hpp>
#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NBaseActions.h"
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NRoomRouter.h"
#include "ndmspc/http/NX509Authenticator.h"
#include "ndmspc/http/NX509Config.h"

#include "ndmspc/ndmspc.h"

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
  std::string macroFilename;
  app.add_option("-m,--macro", macroFilename,
                 "Macro path list separated by commas (default: auto-load "
                 "$NDMSPC_DIR/macros/tools/toolNgnt.C; the base actions are built in). Ignored with "
                 "--rooms, which serves rooms only");
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
  // /api/room/* and creates one Knative Service per room.
  bool withRooms = false;
  if (const char * roomsEnv = std::getenv("NDMSPC_ROOMS"); roomsEnv != nullptr && *roomsEnv != '\0') {
    withRooms = Ndmspc::NUtils::ParseBoolEnv(roomsEnv);
  }
  app.add_option("--rooms", withRooms,
                 "Also serve the room router (NRoomRouter): /api/room/*, one Knative Service "
                 "per room, and the room websocket policy; disabled by default (--rooms true "
                 "or NDMSPC_ROOMS=1). A router serves rooms only, so no macro is loaded. "
                 "Kubernetes only: the server exits at startup when KUBERNETES_SERVICE_HOST is unset")
      ->default_val(withRooms ? "true" : "false");
  AddOidcOptions(&app, oidcConfig);
  AddX509Options(&app, x509Config);

  app.callback([&rootApp, &port, &macroFilename, &batch, &htmlDir, &noHistory, &heartbeat_ms, &withMcp,
                &withRooms, &withWs, &oidcConfig, &x509Config]() {
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
    // with rooms loads no macro at all (the room router serves /api/room/* only), so it is resolved
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
        // The base actions (health, state) are built into the server, so only the tools a
        // deployment actually wants (ngnt above all) have to be named here.
        macroFilename = TString::Format("%s/macros/tools/toolNgnt.C", ndmspcMacrosDir.c_str()).Data();
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
    // here so they exist whatever -m says. A room router serves /api/room/* only, so it gets none.
    if (!withRooms) Ndmspc::RegisterBaseActions();

    // A router serves rooms, not tools, so it loads no macro at all - not even one named on the
    // command line (a deployment's image CMD passes -m .../toolNgnt.C, which must not reach the
    // entry: the room router is rooms and nothing else).
    std::vector<std::string> macros;
    if (withRooms) {
      if (!macroFilename.empty()) {
        NLogWarning("--rooms: ignoring the macro list '%s'; the room router serves only /api/room/*",
                    macroFilename.c_str());
      }
      NLogInfo("Rooms enabled: serving the room actions only (no tool macro is loaded)");
    }
    else {
      macros = Ndmspc::NUtils::Tokenize(macroFilename, ',');

      NLogInfo("Going to load %d macro(s). Waiting ...", static_cast<int>(macros.size()));
      for (const auto & macro : macros) {
        // NLogInfo("Executing macro: %s", macro.c_str());
        TMacro * m = Ndmspc::NUtils::OpenMacro(macro);
        // OpenMacro logs why it could not read the file and answers nullptr; dereferencing that is a
        // startup segfault, which says nothing about what was wrong with the path.
        if (m == nullptr) {
          NLogError("Cannot load macro '%s'. Check the path, or give the macro to load with -m.", macro.c_str());
          exit(1);
        }
        m->Exec();
      }

      NLogInfo("%zu macro(s) executed.", macros.size());
    }

    if (!Ndmspc::gNdmspcHttpHandlers) {
      return;
    }

    if (withRooms) {
      // The room router is framework code (NRoomRouter), not a macro: register its actions before
      // the handler map is handed to the server, so nothing races with the engine start below.
      if (!Ndmspc::NRoomRouter::Instance().Register(serv)) exit(1); // it logged why
      const Ndmspc::NRoomConfig roomCfg = Ndmspc::NRoomConfig::FromEnv();
      NLogInfo("Rooms enabled: serving /api/room/* from the room router in namespace '%s' "
               "(one Knative Service per room, named '%s<id>')",
               roomCfg.ns.c_str(), roomCfg.prefix.c_str());
    }

    serv->SetHttpHandlers(handlers);

    // All handlers are registered now: start the HTTP engine. Nothing has been
    // listening up to this point, so no request could have raced with the
    // handler-map population above.
    if (x509Config.Enabled()) {
      // X509 (mutual TLS) mode: the ROOT engine stays private on loopback with
      // OIDC disabled; the httplib front-door on the public port terminates TLS,
      // verifies client certificates, extracts the subject as the username, and
      // forwards HTTP + WebSocket traffic here.
      const std::string internalBase = TString::Format("http://127.0.0.1:%d", x509Config.internalPort).Data();
      serv->StartEngine(TString::Format("http:127.0.0.1:%d?top=ndmspc", x509Config.internalPort).Data());
      EnsureServerRunning(serv, x509Config.internalPort);
      if (serv->IsTerminated()) {
        NLogError("Server is zombie, exiting ...");
        exit(1);
      }
      NLogInfo("Internal ROOT engine listening on loopback port %d", x509Config.internalPort);

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

    serv->StartEngine(TString::Format("http:%d?top=ndmspc", port).Data());
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
