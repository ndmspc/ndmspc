#include <CLI11.hpp>
#include <NLogger.h>
#include <TSystem.h>
#include <TFile.h>
#include <TApplication.h>
#include "NUtils.h"
#include "NHttpServer.h"
#include "NWsHandler.h"
#include "NStressHistograms.h"
#include "NLogger.h"
#include "ndmspc.h"
std::string app_description()
{
  size_t size = 64;
  auto   buf  = std::make_unique<char[]>(size);
  // char   buf[size];
  size = std::snprintf(buf.get(), size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

int main(int argc, char ** argv)
{

  TApplication rootApp("myapp", &argc, argv);
  if (getenv("NDMSPC_CACHE")) {
    std::string              cache               = getenv("NDMSPC_CACHE");
    std::vector<std::string> cacheOpts           = Ndmspc::NUtils::Tokenize(cache.c_str(), ':');
    std::string              cacheDir            = cacheOpts[0];
    int                      operateDisconnected = atoi(cacheOpts[1].c_str());
    int                      forceCacheRead      = atoi(cacheOpts[2].c_str());
    TFile::SetCacheFileDir(gSystem->ExpandPathName(cacheDir.c_str()), operateDisconnected, forceCacheRead);
  }

  CLI::App app{app_description()};
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
  auto server_default_fun = ([&rootApp]() {
    int port = 8080;
    if (gSystem->Getenv("PORT")) {
      port = atoi(gSystem->Getenv("PORT"));
    }

    Ndmspc::NHttpServer * serv = new Ndmspc::NHttpServer(TString::Format("http:%d?top=ndmspc", port).Data());
    if (serv == nullptr) {
      NLogError("Server was not created !!!");
      exit(1);
    }

    // int timeout = 100;
    // serv->SetTimer(0, kTRUE);
    // press Ctrl-C to stop macro
    // while (!gSystem->ProcessEvents()) {
    //   // NLogDebug("Waiting for requests ...");
    //   gSystem->Sleep(timeout);
    // }
    //
    NLogInfo("Starting server on port %d ...", port);
    rootApp.Run();
  });

  server_default->callback(server_default_fun);
  // server_default->enabled_by_default();
  CLI::App * server_stress = server->add_subcommand("stress", "Stress http server");
  if (server_stress == nullptr) {
    NLogError("Problem creating serve stress subcommand");
    return 1;
  }
  int fill = 1;
  server_stress->add_option("-f,--fill", fill, "N fill (default: 1)");
  int timeout = 100;
  server_stress->add_option("-t,--timeout", timeout, "Publish timeout in miliseconds (default: 100)");
  int reset = 100;
  server_stress->add_option("-r,--reset", reset, "Reset every n events (default: 100)");
  int seed = 0;
  server_stress->add_option("-s,--seed", seed, "Random seed (default: 0)");
  bool batch = false;
  server_stress->add_option("-b,--batch", batch, "Batch mode without graphics (default: false)");

  server_stress->callback([&rootApp, &fill, &timeout, &reset, &seed, &batch]() {
    NLogInfo("Using stress processing method.");
    NLogInfo("Parameters: fill=%d timeout=%d reset=%d seed=%d batch=%d", fill, timeout, reset, seed, batch);
    int port = 8080;

    if (gSystem->Getenv("PORT")) {
      port = atoi(gSystem->Getenv("PORT"));
    }

    Ndmspc::NHttpServer * serv = new Ndmspc::NHttpServer(TString::Format("http:%d?top=ndmspc", port).Data());
    NLogInfo("Starting server on port %d ...", port);
    Ndmspc::NWsHandler * ws = serv->GetWebSocketHandler();

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
