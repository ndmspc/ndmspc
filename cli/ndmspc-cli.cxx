#include <CLI11.hpp>
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
  char   buf[size];
  size = std::snprintf(buf, size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf, size);
}

int main(int argc, char ** argv)
{

  CLI::App app{app_description()};
  app.require_subcommand(); // 1 or more
  argv = app.ensure_utf8(argv);
  app.set_help_all_flag("--help-all", "Expand all help");

  CLI::App * serve = app.add_subcommand("serve", "Http Server");
  serve->require_subcommand(); // 1 or more
  CLI::App * serve_default = serve->add_subcommand("default", "Default http server");
  if (serve_default == nullptr) {
    return 1;
  }
  CLI::App * serve_stress = serve->add_subcommand("stress", "Stress http server");
  if (serve_stress == nullptr) {
    Printf("Problem creating serve stress subcommand");
    return 1;
  }
  int fill = 1;
  serve_stress->add_option("-f,--fill", fill, "N fill (default: 1)");
  int timeout = 100;
  serve_stress->add_option("-t,--timeout", timeout, "Publish timeout in miliseconds (default: 100)");
  int reset = 100;
  serve_stress->add_option("-r,--reset", reset, "Reset every n events (default: 100)");
  int seed = 0;
  serve_stress->add_option("-s,--seed", seed, "Random seed (default: 0)");
  bool batch = false;
  serve_stress->add_option("-b,--batch", batch, "Batch mode without graphics (default: false)");

  CLI11_PARSE(app, argc, argv);
  // if (getenv("NDMSPC_POINT_NAME")) {
  //   if (name.empty()) name = getenv("NDMSPC_POINT_NAME");
  // }
  if (getenv("NDMSPC_CACHE")) {
    std::string              cache               = getenv("NDMSPC_CACHE");
    std::vector<std::string> cacheOpts           = Ndmspc::NUtils::Tokenize(cache.c_str(), ':');
    std::string              cacheDir            = cacheOpts[0];
    int                      operateDisconnected = atoi(cacheOpts[1].c_str());
    int                      forceCacheRead      = atoi(cacheOpts[2].c_str());
    TFile::SetCacheFileDir(gSystem->ExpandPathName(cacheDir.c_str()), operateDisconnected, forceCacheRead);
  }

  for (auto * subcom : app.get_subcommands()) {
    // std::cout << "Subcommand: " << subcom->get_name() << std::endl;
    if (!subcom->get_name().compare("version")) {
      Printf("%s", app_description().c_str());
    }
    if (!subcom->get_name().compare("serve")) {
      for (auto * subsubcom : subcom->get_subcommands()) {
        if (!subsubcom->get_name().compare("default")) {
          TApplication app("myapp", &argc, argv);
          int          port = 8080;
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
          app.Run();
        }
        else if (!subsubcom->get_name().compare("stress")) {
          TApplication app("myapp", &argc, argv);
          int          port = 8080;

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
          app.Run();
        }
      }
    }
  };

  // Printf("Using config file '%s' ...", filename.c_str());
  return 0;
}
