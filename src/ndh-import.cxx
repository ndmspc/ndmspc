#include <getopt.h>
#include <iostream>
#include <TFile.h>
#include <TTree.h>
#include <fstream>

#include <nlohmann/json.hpp>

#include "HnSparse.h"
#include "ndmspc.h"

using json = nlohmann::json;

void version()
{
  Printf("%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
}

[[noreturn]] void help(int rc = 0)
{
  version();
  Printf("\nUsage: [OPTION]...");
  Printf("\nOptions:");
  Printf("       -c, --config[=VALUE]          ndh config file");
  Printf("\n       -h, --help                    display this help and exit");
  Printf("       -v, --version                 output version information and exit");
  Printf("\nExamples:");
  Printf("       %s-import -c ndh.yaml", NDMSPC_NAME);
  Printf("                                     Using ndh.yaml config file");
  Printf("\nReport bugs at <https://gitlab.com/ndmspc/ndmspc>");
  Printf("General help using GNU software: <https://www.gnu.org/gethelp/>");

  exit(rc);
}

int main(int argc, char ** argv)
{

  // ***** Default values START *****
  /// Config file
  std::string        configFile     = "";
  std::string        filename       = "";
  std::string        objname        = "";
  std::vector<Int_t> axisSplit      = {};
  std::string        outputFilename = "";

  json config;
  config["input"]["filename"] =
      "root://eos.ndmspc.io//eos/ndmspc/scratch/alice.cern.ch/user/a/alitrain/PWGLF/LF_pp_AOD/"
      "987/phi_leading_3s/AnalysisResults.root";
  config["input"]["objname"]   = "Unlike";
  config["split"]["axes"]      = {1, 2};
  config["output"]["filename"] = "/tmp/ndh.root";

  // ***** Default values END *****

  std::string   shortOpts  = "hvc:W;";
  struct option longOpts[] = {{"help", no_argument, nullptr, 'h'},
                              {"version", no_argument, nullptr, 'v'},
                              {"config", required_argument, nullptr, 'c'},
                              {nullptr, 0, nullptr, 0}};

  int nextOption = 0;
  do {
    nextOption = getopt_long(argc, argv, shortOpts.c_str(), longOpts, nullptr);
    switch (nextOption) {
    case -1:
    case 0: break;
    case 'h': help();
    case 'v':
      version();
      exit(0);
      break;
    case 'c': configFile = optarg; break;
    default: help(1);
    }
  } while (nextOption != -1);

  version();

  if (!configFile.empty()) {
    Printf("Loading config file '%s' ...", configFile.data());
    std::ifstream f(configFile);
    if (f.fail()) {
      Printf("Error: Cannot open config file '%s' !!!", configFile.data());
    }
    json cfgJson = json::parse(f);
    config.merge_patch(cfgJson);
  }

  std::cout << config << std::endl;

  filename       = config["input"]["filename"].get<std::string>();
  objname        = config["input"]["objname"].get<std::string>();
  axisSplit      = config["split"]["axes"].get<std::vector<int>>();
  outputFilename = config["output"]["filename"].get<std::string>();

  Ndmspc::Ndh::HnSparseL h;
  h.SetOutputFileName(outputFilename.data());
  h.Import(axisSplit, filename.data(), objname.data());

  TFile *     f  = TFile::Open(outputFilename.data());
  TTree *     t  = (TTree *)f->Get("ndh");
  THnSparse * ss = nullptr;
  t->SetBranchAddress("h", &ss);

  for (Int_t i = 0; i < t->GetEntries(); i++) {
    t->GetEntry(i);
    ss->Print();
  }

  t->GetUserInfo()->Print();

  return 0;
}
