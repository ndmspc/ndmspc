#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TString.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>
#include <TCanvas.h>
#include <THnSparse.h>
#include <TBase64.h>

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
json cfg;
int  NdmspcGenerateJobs(std::string configFileName = "myAnalysis.json",
                        std::string outputFileName = "/tmp/ndmspc-jobs.txt", std::string ndmscpRunScript = "ndmspc")
{
  std::ifstream configFile(configFileName);
  if (configFile.is_open()) {
    cfg = json::parse(configFile);
  }
  else {
    Printf("Error: Config file '%s' was not found !!!", configFileName.c_str());
    return 1;
  }
  std::ofstream outputFile;
  outputFile.open(outputFileName.c_str());
  if (!outputFile.is_open()) {
    Printf("Error: Problem opening output file '%s' !!!", outputFileName.c_str());
    return 2;
  }

  std::string inputs;
  for (auto & ji : cfg["ndmspc"]["job"]["inputs"]) {
    inputs += "'" + ji.get<std::string>() + "' ";
  }

  json dataHistogramBins = cfg["ndmspc"]["data"]["histogram"]["bins"];

  for (auto & dhb : dataHistogramBins) {
    cfg["ndmspc"]["data"]["histogram"]["bins"] = json::array();
    cfg["ndmspc"]["data"]["histogram"]["bins"].push_back(dhb);

    outputFile << ndmscpRunScript << " _macro " << gSystem->Getenv("NDMSPC_MACRO_DIR")
               << "/NdmspcPointRun.C '\"ndmspc.json\",true' " << inputs << " 'base64|"
               << TBase64::Encode(cfg.dump().c_str()).Data() << "|ndmspc.json'" << std::endl;
  }

  outputFile.close();
  return 0;
}