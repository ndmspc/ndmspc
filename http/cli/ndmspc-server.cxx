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
#include "ndmspc/http/NStressHistograms.h"
#include "ndmspc/http/NGnHttpServer.h"
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

void log_server_version(const char * mode, int port)
{
  NLogInfo("Starting %s server on port %d with %s", mode, port, app_version().c_str());
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
  app.require_subcommand(1); // 1 or more
  argv = app.ensure_utf8(argv);
  app.set_help_all_flag("--help-all", "Expand all help");

  CLI::App * server = app.add_subcommand("start", "Http Server");
  // server->fallthrough();
  // server->require_subcommand(1); // 1 or more
  CLI::App * server_default = server->add_subcommand("default", "Default http server");
  if (server_default == nullptr) {
    return 1;
  }
  server_default->add_option("-p,--port", port, "Server port (default: 8080)");
  server_default->add_option("-b,--batch", batch, "Batch mode without graphics (default: true)");
  AddOidcOptions(server_default, oidcConfig);
  AddX509Options(server_default, x509Config);
  auto server_default_fun = ([&rootApp, &port, &oidcConfig, &x509Config]() {
    PrepareOidcConfig(oidcConfig, x509Config);
    if (x509Config.Enabled()) {
      NLogError("X509 mode is only supported by the 'start ngnt' subcommand");
      exit(1);
    }
    Ndmspc::NHttpServer * serv = new Ndmspc::NHttpServer(TString::Format("http:%d?top=ndmspc", port).Data(), true, 10000, oidcConfig);
    if (serv == nullptr) {
      NLogError("Server was not created !!!");
      exit(1);
    }
    EnsureServerRunning(serv, port);
    serv->SetCors("*");
    log_server_version("default", port);
    // // This allows ROOT to process system signals like SIGTERM
    // int timeout = 100;
    // // serv->SetTimer(0, kTRUE);
    // // press Ctrl-C to stop macro
    // while (!gSystem->ProcessEvents()) {
    //   // NLogDebug("Waiting for requests ...");
    //   gSystem->Sleep(timeout);
    // }

    // gSystem->AddSignalHandler(new TSignalHandler(kSigTermination, kTRUE));
    //
    // // 3. Optional: Define what happens on exit
    // std::cout << "Server started. Send SIGTERM to exit." << std::endl;
    //
    // // 4. Use gSystem->Run() which handles the event loop correctly
    // // It will return when a signal is received if the handler is set to kTRUE
    // gSystem->Run();
    //
    // std::cout << "Shutting down gracefully..." << std::endl;
    // delete serv;
    // gApplication->Terminate(0);

    // NLogInfo("Starting server on port %d ...", port);
    rootApp.Run();
  });

  server_default->callback(server_default_fun);
  // server_default->enabled_by_default();
  CLI::App * server_stress = server->add_subcommand("stress", "Stress http server");
  if (server_stress == nullptr) {
    NLogError("Problem creating serve stress subcommand");
    return 1;
  }
  server_stress->add_option("-p,--port", port, "Server port (default: 8080)");
  int fill = 1;
  server_stress->add_option("-f,--fill", fill, "N fill (default: 1)");
  int timeout = 100;
  server_stress->add_option("-t,--timeout", timeout, "Publish timeout in miliseconds (default: 100)");
  int reset = 100;
  server_stress->add_option("-r,--reset", reset, "Reset every n events (default: 100)");
  int seed = 0;
  server_stress->add_option("-s,--seed", seed, "Random seed (default: 0)");
  server_stress->add_option("-b,--batch", batch, "Batch mode without graphics (default: false)");
  AddOidcOptions(server_stress, oidcConfig);
  AddX509Options(server_stress, x509Config);
  server_stress->callback([&rootApp, &port, &fill, &timeout, &reset, &seed, &batch, &oidcConfig, &x509Config]() {
    NLogInfo("Using stress processing method.");
    NLogInfo("Parameters: fill=%d timeout=%d reset=%d seed=%d batch=%d", fill, timeout, reset, seed, batch);

    gROOT->SetBatch(batch);
    PrepareOidcConfig(oidcConfig, x509Config);
    if (x509Config.Enabled()) {
      NLogError("X509 mode is only supported by the 'start ngnt' subcommand");
      exit(1);
    }
    Ndmspc::NHttpServer * serv = new Ndmspc::NHttpServer(TString::Format("http:%d?top=ndmspc", port).Data(), true, 10000, oidcConfig);
    if (serv == nullptr) {
      NLogError("Server was not created !!!");
      exit(1);
    }
    EnsureServerRunning(serv, port);
    serv->SetCors("*");
    log_server_version("stress", port);
    Ndmspc::NWsHandler * ws = serv->GetWebSocketHandler();
    // This allows ROOT to process system signals like SIGTERM
    // gSystem->AddSignalHandler(new TSignalHandler(kSigTermination, kTRUE));
    // when read-only mode disabled one could execute object methods like TTree::Draw()
    serv->SetReadOnly(kFALSE);

    Ndmspc::NStressHistograms sh(fill, reset, seed, batch);

    // press Ctrl-C to stop macro
    while (!gSystem->ProcessEvents()) {
      if (!sh.HandleEvent(ws)) break;
      gSystem->Sleep(timeout);
    }
    rootApp.Run();
  });

  CLI::App * server_ngnt = server->add_subcommand("ngnt", "NGnTree http server");
  if (server_ngnt == nullptr) {
    NLogError("Problem creating serve ngnt subcommand");
    return 1;
  }
  server_ngnt->add_option("-p,--port", port, "Server port (default: 8080)");
  // add file url option
  std::string macroFilename;
  server_ngnt->add_option("-m,--macro", macroFilename,
                          "Macro path list separated by commas (default: auto-load "
                          "$NDMSPC_DIR/macros/builtin/httpNgntBase.C,$NDMSPC_DIR/macros/builtin/httpNgnt.C)");
  server_ngnt->add_option("-b,--batch", batch, "Batch mode without graphics (default: true)");
  std::string htmlDir = "";
  server_ngnt->add_option("--html", htmlDir, "Directory with static assets (default: empty, use built-in)");
  bool noHistory = false;
  server_ngnt->add_option("--no-history", noHistory, "Disable history in processing requests")->default_val("false");
  int heartbeat_ms = 10000;
  server_ngnt->add_option("--heartbeat", heartbeat_ms, "Heartbeat interval in milliseconds (default: 10000)");
  bool withMcp = false;
  if (const char * mcpEnv = std::getenv("NDMSPC_MCP"); mcpEnv != nullptr && *mcpEnv != '\0') {
    withMcp = Ndmspc::NUtils::ParseBoolEnv(mcpEnv);
  }
  server_ngnt->add_flag("--with-mcp", withMcp,
                        "Expose the MCP endpoint (POST /api/mcp); disabled by default (NDMSPC_MCP=1 enables)");
  AddOidcOptions(server_ngnt, oidcConfig);
  AddX509Options(server_ngnt, x509Config);

  server_ngnt->callback([&rootApp, &port, &macroFilename, &batch, &htmlDir, &noHistory, &heartbeat_ms, &withMcp, &oidcConfig, &x509Config]() {
    gROOT->SetBatch(batch);
    PrepareOidcConfig(oidcConfig, x509Config);

    Ndmspc::NGnHttpServer * serv =
        new Ndmspc::NGnHttpServer("", true, heartbeat_ms, oidcConfig, false);
    if (serv == nullptr) {
      NLogError("Server was not created !!!");
      exit(1);
    }
    log_server_version("ngnt", port);

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

    if (macroFilename.empty()) {
      const char * env1      = gSystem->Getenv("NDMSPC_DIR");
      std::string  ndmspcDir = (env1 && *env1) ? env1 : "";

      if (ndmspcDir.empty()) {
        const char * env2 = gSystem->Getenv("NDMSPC__HOME");
        ndmspcDir         = (env2 && *env2) ? env2 : "/usr/share/ndmspc";
      }
      // check if ndmspcDir is exists
      if (gSystem->AccessPathName(ndmspcDir.c_str()) == 0) {
        macroFilename = TString::Format("%s/macros/builtin/httpNgntBase.C,%s/macros/builtin/httpNgnt.C",
                                        ndmspcDir.c_str(), ndmspcDir.c_str())
                            .Data();
        NLogInfo("No macro file given, using default macros ...");
      } else {
        // just warn and continue, user may provide macro file later
        NLogError("No macro file given and default macros not found in '%s'. Please provide a macro file with -m option.", ndmspcDir.c_str());
        exit(1);
      }
    }

    NLogInfo("NDMSPC server heartbeat: %d ms", heartbeat_ms);

    // Your local map
    std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers;
    // Set the global pointer to your local map
    Ndmspc::gNdmspcHttpHandlers = &handlers;
    // MCP tool metadata declared by the macros (descriptions, allowed verbs, ...)
    Ndmspc::NMcpToolMap mcpTools;
    Ndmspc::gNdmspcMcpTools = &mcpTools;

    std::vector<std::string> macros = Ndmspc::NUtils::Tokenize(macroFilename, ',');

    NLogInfo("Going to load %d macro(s). Waiting ...", static_cast<int>(macros.size()));
    for (const auto & macro : macros) {
      // NLogInfo("Executing macro: %s", macro.c_str());
      TMacro * m = Ndmspc::NUtils::OpenMacro(macro);
      m->Exec();
    }

    if (!Ndmspc::gNdmspcHttpHandlers) {
      return;
    }

    NLogInfo("%zu macro(s) executed.", macros.size());
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
    if (server->parsed() && !server_default->parsed() && !server_stress->parsed()) {
      // start default if no subcommand given
      server_default_fun();
    }
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
