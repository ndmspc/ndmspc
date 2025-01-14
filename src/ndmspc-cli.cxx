#include <getopt.h>
#include <cstdlib>
#include <string>
#include <CLI11.hpp>
#include "TSystem.h"
#include <TString.h>
#include <TStopwatch.h>
#include <TApplication.h>

#include "Results.h"
#include "ndmspc.h"
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
  // serve->require_subcommand(); // 1 or more

  CLI::App * browser = app.add_subcommand("browser", "Object browser");
  browser->require_subcommand(); // 1 or more

  CLI::App * browser_hnsparse = browser->add_subcommand("hnsparse", "HnSparse browser");
  browser_hnsparse->add_option("-f,--file", fileName, "Input file");
  browser_hnsparse->add_option("-o,--objects", objectName, "Input objects");
  browser_hnsparse->add_option("-t,--token", directoryToken, "Directory token (default: '/')");

  CLI::App * browser_result = browser->add_subcommand("result", "NdmSpc result browser");
  browser_result->add_option("-c,--config", configFileName, "Config file name");
  browser_result->add_option("-u,--user-config", userConfigFileName, "User config file name");
  browser_result->add_option("-r,--user-config-raw", userConfigRaw, "User config raw");
  browser_result->add_option("-e,--environement", environement, "environement");

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
          NdmSpc::PointRun::Generate(name, fileName, objectName);
        }
        if (!subsubcom->get_name().compare("run")) {
          TStopwatch timer;
          timer.Start();
          NdmSpc::PointRun pr(macroFileName);
          pr.Run(configFileName, userConfigFileName, environement, userConfigRaw, false);
          timer.Stop();
          timer.Print();
        }
        if (!subsubcom->get_name().compare("merge")) {
          TStopwatch timer;
          timer.Start();
          NdmSpc::PointRun::Merge(configFileName, userConfigFileName, environement, userConfigRaw);
          timer.Stop();
          timer.Print();
        }
        if (!subsubcom->get_name().compare("draw")) {
          NdmSpc::PointDraw pd;
          pd.Draw(configFileName, userConfigFileName, environement, userConfigRaw);
        }
      }
    }
    if (!subcom->get_name().compare("browser")) {
      for (auto * subsubcom : subcom->get_subcommands()) {
        if (!subsubcom->get_name().compare("hnsparse")) {
          NdmSpc::HnSparseBrowser browser;
          browser.Draw(fileName, objectName, directoryToken);
        }
        if (!subsubcom->get_name().compare("result")) {
          NdmSpc::Results result;
          result.LoadConfig(configFileName, userConfigFileName, environement, userConfigRaw);
          result.Draw();
        }
      }
    }
    if (!subcom->get_name().compare("serve")) {
      TApplication app("myapp", &argc, argv);
      int          port = 8080;
      if (gSystem->Getenv("PORT")) {
        port = atoi(gSystem->Getenv("PORT"));
      }

      NdmSpc::HttpServer s(TString::Format("http:%d", port).Data());
      app.Run();
    }
  };

  // Printf("Using config file '%s' ...", filename.c_str());
  return 0;
}
