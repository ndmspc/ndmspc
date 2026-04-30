#include <CLI11.hpp>
#include <csignal>
#include "TROOT.h"
#include <NLogger.h>
#include <TSystem.h>
#include <TFile.h>
#include <TApplication.h>
#include "NUtils.h"
#include "NStressHistograms.h"
#include "NGnHttpServer.h"
#include "NLogger.h"
#include "ndmspc.h"

static inline void EnsureServerRunning(Ndmspc::NHttpServer *serv, int port)
{
  if (!serv || !serv->IsAnyEngine()) {
    NLogError("Server is not running on port %d: address may be in use.\n"
              "Hints:\n"
              " - Check for a process listening on the port: `lsof -i :%d` or `ss -ltnp | grep :%d`.\n"
              " - Check other terminals or system services (systemd) that may have started the server.\n"
              " - Check for Docker containers exposing the port: `docker ps --format '{{.ID}} {{.Names}} {{.Ports}}' | grep :%d`.\n"
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
  size = std::snprintf(buf.get(), size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
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

int main(int argc, char ** argv)
{

  // Or standard C:
  signal(SIGTERM, handle_sigterm);

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
  auto server_default_fun = ([&rootApp, &port]() {
    Ndmspc::NHttpServer * serv = new Ndmspc::NHttpServer(TString::Format("http:%d?top=ndmspc", port).Data());
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
  server_stress->callback([&rootApp, &port, &fill, &timeout, &reset, &seed, &batch]() {
    NLogInfo("Using stress processing method.");
    NLogInfo("Parameters: fill=%d timeout=%d reset=%d seed=%d batch=%d", fill, timeout, reset, seed, batch);

    gROOT->SetBatch(batch);
    Ndmspc::NHttpServer * serv = new Ndmspc::NHttpServer(TString::Format("http:%d?top=ndmspc", port).Data());
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
                          "Macro path list separated by commas (default: auto-load $NDMSPC_DIR/macros/builtin/httpNgntBase.C,$NDMSPC_DIR/macros/builtin/httpNgnt.C)");
  server_ngnt->add_option("-b,--batch", batch, "Batch mode without graphics (default: true)");
  std::string htmlDir = "";
  server_ngnt->add_option("--html", htmlDir, "Directory with static assets (default: empty, use built-in)");
  bool noHistory = false;
  server_ngnt->add_option("--no-history", noHistory, "Disable history in processing requests")->default_val("false");
  int heartbeat_ms = 10000;
  server_ngnt->add_option("--heartbeat", heartbeat_ms, "Heartbeat interval in milliseconds (default: 10000)");

  server_ngnt->callback([&rootApp, &port, &macroFilename, &batch, &htmlDir, &noHistory, &heartbeat_ms]() {
    gROOT->SetBatch(batch);

    Ndmspc::NGnHttpServer * serv =
        new Ndmspc::NGnHttpServer(TString::Format("http:%d?top=ndmspc", port).Data(), true, heartbeat_ms);
    if (serv == nullptr) {
      NLogError("Server was not created !!!");
      exit(1);
    }
    EnsureServerRunning(serv, port);
    log_server_version("ngnt", port);

    serv->SetUseHistory(!noHistory);
    serv->SetCors("*");
    if (!htmlDir.empty()) {
      NLogInfo("Using '%s' as directory with static assets.", htmlDir.c_str());
      serv->AddLocation("assets/", TString::Format("%s/assets", htmlDir.c_str()).Data());
      serv->SetDefaultPage(TString::Format("%s/index.html", htmlDir.c_str()).Data());
    }

    if (macroFilename.empty()) {
      const char * ndmspcDir = gSystem->Getenv("NDMSPC_DIR");
      std::string  baseDir   = (ndmspcDir && *ndmspcDir) ? ndmspcDir : "/usr/share/ndmspc";
      macroFilename = TString::Format("%s/macros/builtin/httpNgntBase.C,%s/macros/builtin/httpNgnt.C", baseDir.c_str(),
                                       baseDir.c_str())
                          .Data();
      NLogInfo("No macro file given, using default NGNT macros: '%s'", macroFilename.c_str());
    }

    NLogInfo("NGNT server heartbeat: %d ms, macro file '%s'", heartbeat_ms,
             macroFilename.c_str());

    // Your local map
    std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers;
    // Set the global pointer to your local map
    Ndmspc::gNdmspcHttpHandlers = &handlers;

    std::vector<std::string> macros = Ndmspc::NUtils::Tokenize(macroFilename, ',');

    for (const auto & macro : macros) {
      NLogInfo("Executing macro: %s", macro.c_str());
      TMacro * m = Ndmspc::NUtils::OpenMacro(macro);
      m->Exec();
    }

    if (!Ndmspc::gNdmspcHttpHandlers) {
      return;
    }

    NLogInfo("Macro '%s' executed.", macroFilename.c_str());
    serv->SetHttpHandlers(handlers);

    if (serv->IsTerminated()) {
      NLogError("Server is zombie, exiting ...");
      exit(1);
    }

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

  return 0;
}
