#include <getopt.h>
#include <cstddef>
#include <cstdlib>
#include <nlohmann/detail/value_t.hpp>
#include <string>
#include <CLI11.hpp>
#include <vector>
#include "TAxis.h"
#include "TSystem.h"
#include <TString.h>
#include <TStopwatch.h>
#include <TApplication.h>
#include <TRandom3.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TCanvas.h>
#include <TFrame.h>
#include <TBenchmark.h>
#include <TBufferJSON.h>

#include "StressHistograms.h"
#include "ndmspc.h"
#include "Axis.h"
#include "Results.h"
#include "PointRun.h"
#include "PointDraw.h"
#include "HttpServer.h"
#include "HnSparseBrowser.h"

std::string app_description()
{
  size_t size = 64;
  char   buf[size];
  size =
      std::snprintf(buf, size, "%s v%d.%d.%d-%s", NDMSPC_NAME, NDMSPC_VERSION_MAJOR(NDMSPC_VERSION),
                    NDMSPC_VERSION_MINOR(NDMSPC_VERSION), NDMSPC_VERSION_PATCH(NDMSPC_VERSION), NDMSPC_VERSION_RELEASE);
  return std::string(buf, size);
}

int main(int argc, char ** argv)
{
  CLI::App app{app_description()};
  app.require_subcommand(); // 1 or more
  argv = app.ensure_utf8(argv);
  app.set_help_all_flag("--help-all", "Expand all help");

  std::string name               = "";
  std::string basedir            = "";
  std::string fileName           = "";
  std::string objectName         = "";
  std::string configFileName     = "";
  std::string userConfigFileName = "";
  std::string userConfigRaw      = "";
  std::string macroFileName      = "";
  std::string directoryToken     = "";
  std::string environement       = "";
  std::string jobs               = "";
  std::string jobdir             = "/tmp/ndmspc-jobs";
  std::string cutBaseAxis        = "";
  std::string cutRanges          = "";

  /*app.add_option("-c,--config", configFileName, "Config file name");*/

  CLI::App * point = app.add_subcommand("point", "Point");
  point->require_subcommand(); // 1 or more
  point->add_option("-c,--config", configFileName, "Config file name");
  point->add_option("-u,--user-config", userConfigFileName, "User config file name");
  point->add_option("-r,--user-config-raw", userConfigRaw, "User config raw");
  point->add_option("-m,--macro", macroFileName, "Macro path");
  point->add_option("-e,--environement", environement, "environement");

  CLI::App * point_gen = point->add_subcommand("gen", "Point generate");
  point_gen->add_option("-n,--name", name, "Name");
  point_gen->add_option("-f,--file", fileName, "Input file");
  point_gen->add_option("-o,--object", objectName, "Input object");

  CLI::App * point_run = point->add_subcommand("run", "Point run");
  point_run->add_option("-n,--name", name, "Name");
  point_run->add_option("-d,--basedir", basedir, "Base dir");
  point_run->add_option("-c,--config", configFileName, "Config file name");
  point_run->add_option("-u,--user-config", userConfigFileName, "User config file name");
  point_run->add_option("-r,--user-config-raw", userConfigRaw, "User config raw");
  point_run->add_option("-m,--macro", macroFileName, "Macro path");
  point_run->add_option("-e,--environement", environement, "environement");
  point_run->add_option("-j,--jobs", jobs, "Generate jobs");
  point_run->add_option("-o,--output-dir", jobdir, "Generate jobs output dir");

  CLI::App * point_merge = point->add_subcommand("merge", "Point merge");
  point_merge->add_option("-n,--name", name, "Name");
  point_merge->add_option("-d,--basedir", basedir, "Base dir");
  point_merge->add_option("-c,--config", configFileName, "Config file name");
  point_merge->add_option("-u,--user-config", userConfigFileName, "User config file name");
  point_merge->add_option("-r,--user-config-raw", userConfigRaw, "User config raw");
  point_merge->add_option("-e,--environement", environement, "environement");

  CLI::App * point_draw = point->add_subcommand("draw", "Point draw");
  point_draw->add_option("-n,--name", name, "Name");
  point_draw->add_option("-d,--basedir", basedir, "Base dir");
  point_draw->add_option("-c,--config", configFileName, "Config file name");
  point_draw->add_option("-u,--user-config", userConfigFileName, "User config file name");
  point_draw->add_option("-r,--user-config-raw", userConfigRaw, "User config raw");
  point_draw->add_option("-e,--environement", environement, "environement");

  CLI::App * serve = app.add_subcommand("serve", "Http Server");
  serve->require_subcommand(); // 1 or more
  CLI::App * serve_default = serve->add_subcommand("default", "Default http server");
  if (serve_default == nullptr) {
    Printf("Problem creating serve subcommand");
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

  CLI::App * browser = app.add_subcommand("browser", "Object browser");
  browser->require_subcommand(); // 1 or more

  CLI::App * browser_hnsparse = browser->add_subcommand("hnsparse", "HnSparse browser");
  browser_hnsparse->add_option("-f,--file", fileName, "Input file");
  browser_hnsparse->add_option("-o,--objects", objectName, "Input objects");
  browser_hnsparse->add_option("-t,--token", directoryToken, "Directory token (default: '/')");

  CLI::App * browser_result = browser->add_subcommand("result", "Ndmspc result browser");
  browser_result->add_option("-c,--config", configFileName, "Config file name");
  browser_result->add_option("-u,--user-config", userConfigFileName, "User config file name");
  browser_result->add_option("-r,--user-config-raw", userConfigRaw, "User config raw");
  browser_result->add_option("-e,--environement", environement, "environement");

  CLI::App * cuts = app.add_subcommand("cuts", "Cuts");
  cuts->add_option("-b,--base", cutBaseAxis, "Base axis (<nBins>,<min>,<max>)");
  cuts->add_option("-r,--ranges", cutBaseAxis, "Range (<rebin1>:<nbins1>,...,<rebinN>:<nbinsN>)");

  CLI11_PARSE(app, argc, argv);
  if (getenv("NDMSPC_POINT_NAME")) {
    if (name.empty()) name = getenv("NDMSPC_POINT_NAME");
  }
  if (getenv("NDMSPC_POINT_BASEDIR")) {
    if (basedir.empty()) basedir = getenv("NDMSPC_POINT_BASEDIR");
  }
  if (getenv("NDMSPC_POINT_CONFIG")) {
    if (configFileName.empty()) configFileName = getenv("NDMSPC_POINT_CONFIG");
  }
  if (getenv("NDMSPC_POINT_CONFIG_USER")) {
    if (userConfigFileName.empty()) userConfigFileName = getenv("NDMSPC_POINT_CONFIG_USER");
  }
  if (getenv("NDMSPC_POINT_MACRO")) {
    if (macroFileName.empty()) macroFileName = getenv("NDMSPC_POINT_MACRO");
  }
  if (getenv("NDMSPC_POINT_ENVIRONMENT")) {
    if (environement.empty()) environement = getenv("NDMSPC_POINT_ENVIRONMENT");
  }
  if (getenv("NDMSPC_BROWSER_FILE")) {
    if (fileName.empty()) fileName = getenv("NDMSPC_BROWSER_FILE");
  }
  if (getenv("NDMSPC_BROWSER_OBJECTS")) {
    if (objectName.empty()) objectName = getenv("NDMSPC_BROWSER_OBJECTS");
  }

  if (getenv("NDMSPC_BROWSER_DIRECTORY_TOKEN")) {
    if (directoryToken.empty()) directoryToken = getenv("NDMSPC_BROWSER_DIRECTORY_TOKEN");
  }
  if (getenv("NDMSPC_CUTS_BASE_AXIS")) {
    if (cutBaseAxis.empty()) cutBaseAxis = getenv("NDMSPC_CUTS_BASE_AXIS");
  }
  if (getenv("NDMSPC_CUTS_RANGES")) {
    if (cutRanges.empty()) cutRanges = getenv("NDMSPC_CUTS_RANGES");
  }

  if (!basedir.empty()) {
    if (basedir[basedir.size() - 1] != '/') basedir += "/";
    if (!name.empty()) {
      if (configFileName.empty()) configFileName = basedir + name + ".json";
      if (macroFileName.empty()) macroFileName = basedir + name + ".C";
    }
    else {
      configFileName = basedir + configFileName;
      macroFileName  = basedir + macroFileName;
    }
  }

  if (directoryToken.empty()) directoryToken = "/";

  if (environement.empty()) environement = "default";

  // std::cout << "Working on --file from start: " << file << '\n';
  // std::cout << "Working on --count from stop: " << s->count() << ", direct count: " << stop->count("--count") <<
  // '\n'; std::cout << "Count of --random flag: " << app.count("--random") << '\n';
  for (auto * subcom : app.get_subcommands()) {
    // std::cout << "Subcommand: " << subcom->get_name() << std::endl;
    if (!subcom->get_name().compare("point")) {

      for (auto * subsubcom : subcom->get_subcommands()) {
        if (!subsubcom->get_name().compare("gen")) {
          Ndmspc::PointRun::Generate(name, fileName, objectName);
        }
        if (!subsubcom->get_name().compare("run")) {
          TStopwatch timer;
          timer.Start();
          Ndmspc::PointRun pr(macroFileName);
          if (!jobs.empty()) {
            pr.GenerateJobs(jobs, configFileName, userConfigFileName, environement, userConfigRaw, jobdir);
            return 0;
          }
          pr.Run(configFileName, userConfigFileName, environement, userConfigRaw, false);
          timer.Stop();
          timer.Print();
        }
        if (!subsubcom->get_name().compare("merge")) {
          TStopwatch timer;
          timer.Start();
          Ndmspc::PointRun::Merge(configFileName, userConfigFileName, environement, userConfigRaw);
          timer.Stop();
          timer.Print();
        }
        if (!subsubcom->get_name().compare("draw")) {
          Ndmspc::PointDraw pd;
          pd.DrawPoint(configFileName, userConfigFileName, environement, userConfigRaw);
        }
      }
    }
    if (!subcom->get_name().compare("browser")) {
      for (auto * subsubcom : subcom->get_subcommands()) {
        if (!subsubcom->get_name().compare("hnsparse")) {
          Ndmspc::HnSparseBrowser browser;
          browser.DrawBrowser(fileName, objectName, directoryToken);
        }
        if (!subsubcom->get_name().compare("result")) {
          Ndmspc::Results result;
          result.LoadConfig(configFileName, userConfigFileName, environement, userConfigRaw);
          result.Draw();
        }
      }
    }
    if (!subcom->get_name().compare("serve")) {
      for (auto * subsubcom : subcom->get_subcommands()) {
        if (!subsubcom->get_name().compare("default")) {
          TApplication app("myapp", &argc, argv);
          int          port = 8080;
          if (gSystem->Getenv("PORT")) {
            port = atoi(gSystem->Getenv("PORT"));
          }

          Ndmspc::HttpServer * serv = new Ndmspc::HttpServer(TString::Format("http:%d?top=aaa", port).Data());
          if (serv == nullptr) {
            Printf("Server was not created !!!");
            exit(1);
          }
          // press Ctrl-C to stop macro
          while (!gSystem->ProcessEvents()) {
            gSystem->Sleep(100);
          }
          app.Run();
        }
        if (!subsubcom->get_name().compare("stress")) {
          TApplication app("myapp", &argc, argv);
          int          port = 8080;
          if (gSystem->Getenv("PORT")) {
            port = atoi(gSystem->Getenv("PORT"));
          }

          Ndmspc::HttpServer * serv = new Ndmspc::HttpServer(TString::Format("http:%d?top=aaa", port).Data());
          Printf("Starting server on port %d ...", port);
          Ndmspc::WebSocketHandler * ws = serv->GetWebSocketHandler();

          // when read-only mode disabled one could execute object methods like TTree::Draw()
          serv->SetReadOnly(kFALSE);

          Ndmspc::StressHistograms sh(fill, reset, seed, batch);

          // press Ctrl-C to stop macro
          while (!gSystem->ProcessEvents()) {
            if (!sh.HandleEvent(ws)) break;
            gSystem->Sleep(timeout);
          }
          app.Run();
        }
      }
    }
    if (!subcom->get_name().compare("cuts")) {
      TApplication app("myapp", &argc, argv);

      std::vector<std::string> a = Ndmspc::Utils::Tokenize(cutBaseAxis, ',');
      if (a.size() != 3) {
        Printf("Error: Invalid base axis format: %s", cutBaseAxis.c_str());
        return 1;
      }
      TAxis * a1 = new TAxis(atoi(a[0].c_str()), atof(a[1].c_str()), atof(a[2].c_str()));
      a1->SetName("a1");

      Ndmspc::Axis *           axis1       = new Ndmspc::Axis(a1, 1, 0, 1, -1);
      std::vector<std::string> rangesArray = Ndmspc::Utils::Tokenize(cutRanges, ',');
      for (auto r : rangesArray) {
        std::vector<std::string> range = Ndmspc::Utils::Tokenize(r, ':');
        if (range.size() != 2) {
          Printf("Error: Invalid range format: %s", r.c_str());
          return 1;
        }
        axis1->AddRange(atoi(range[0].c_str()), atoi(range[1].c_str()));
      }
      if (!axis1->IsRangeValid()) {
        return 1;
      }

      TAxis * varBinningAxis = new TAxis();
      axis1->FillAxis(varBinningAxis);

      TH1D * h = new TH1D("hAxis",
                          TString::Format("Base %s nbins=%d min=%.2f max=%.2f with=%.2f", a1->GetName(), a1->GetNbins(),
                                          a1->GetXmin(), a1->GetXmax(), a1->GetBinWidth(1))
                              .Data(),
                          varBinningAxis->GetNbins(), varBinningAxis->GetXbins()->GetArray());

      for (int i = 0; i < varBinningAxis->GetNbins(); i++) {
        h->SetBinContent(i + 1, i + 1);
      }
      h->Draw();

      // axis1->Validate();
      // Ndmspc::Cuts cuts;
      // TAxis *      a1 = new TAxis(200, 0, 20);
      // a1->SetName("a1");
      // Ndmspc::Axis * axis1 = new Ndmspc::Axis(a1);
      // axis1->AddChild(2, 0, 1, -1);
      // axis1->AddChild(10, 0, 1, -1);
      // axis1->AddChild(10, 2, 1, -1);
      // axis1->AddChild(10, 9, 1, -1);
      // axis1->Validate();
      // cuts.AddAxis(axis1);
      // TAxis * a2 = new TAxis(100, 0, 100);
      // a2->SetName("a2");
      // Ndmspc::Axis * axis2 = new Ndmspc::Axis(a2);
      // axis2->AddChild(2, 0, 1, -1);
      // axis2->AddChild(2, 1, 1, -1);
      // axis2->AddChild(5, 3, 1, -1);
      // axis2->AddChild(5, 12, 5);
      // axis2->AddChild(7, 15, 3);
      // axis2->Validate();
      // cuts.AddAxis(axis2);
      // cuts.Print("");

      // cuts.Print("ranges");
      app.Run();
    }
  };

  // Printf("Using config file '%s' ...", filename.c_str());
  return 0;
}
