#include <getopt.h>
#include <string>
#include <TString.h>
#include <CLI11.hpp>

#include "ndmspc.h"
#include "PointRun.h"

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

  std::string name           = "myPointMacro";
  std::string fileName       = "gitlab_hist.root";
  std::string objectName     = "hGitlabData";
  std::string configFileName = name + ".json";
  std::string macroFileName  = name + ".C";
  /*app.add_option("-c,--config", configFileName, "Config file name");*/

  CLI::App * point = app.add_subcommand("point", "Point");
  point->require_subcommand(); // 1 or more
  /*point->add_option("-c,--config", configFileName, "Config file name");*/
  /*point->add_option("-m,--macro", macroFileName, "Macro path");*/

  CLI::App * point_gen = point->add_subcommand("gen", "Point generate");
  point_gen->add_option("-n,--name", name, "Name");
  point_gen->add_option("-f,--file", fileName, "Input file");
  point_gen->add_option("-o,--object", objectName, "Input object");

  CLI::App * point_run = point->add_subcommand("run", "Point run");
  point_run->add_option("-c,--config", configFileName, "Config file name");
  point_run->add_option("-m,--macro", macroFileName, "Macro path");

  CLI::App * point_merge = point->add_subcommand("merge", "Point merge");
  point_merge->add_option("-c,--config", configFileName, "Config file name");

  CLI11_PARSE(app, argc, argv);

  // std::cout << "Working on --file from start: " << file << '\n';
  // std::cout << "Working on --count from stop: " << s->count() << ", direct count: " << stop->count("--count") <<
  // '\n'; std::cout << "Count of --random flag: " << app.count("--random") << '\n';
  for (auto * subcom : app.get_subcommands()) {
    std::cout << "Subcommand: " << subcom->get_name() << std::endl;
    if (!subcom->get_name().compare("point")) {

      for (auto * subsubcom : subcom->get_subcommands()) {
        if (!subsubcom->get_name().compare("gen")) {
          NdmSpc::PointRun::Generate(name, fileName, objectName);
        }
        if (!subsubcom->get_name().compare("run")) {
          NdmSpc::PointRun pr(macroFileName);
          pr.Run(configFileName, false);
        }
        if (!subsubcom->get_name().compare("merge")) {
          NdmSpc::PointRun::Merge(configFileName);
        }
      }
    }
  };

  // Printf("Using config file '%s' ...", filename.c_str());
  return 0;
}
