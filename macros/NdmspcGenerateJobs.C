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
json             cfg;
std::ofstream    outputFile;
std::string      inputs;
std::vector<int> nBins      = {200, 50};
std::vector<int> nBinWidths = {2, 2};

void SinglePoint(int iCut, int iEnabledCut, int max, std::string & ndmscpRunScript)
{
  // Printf("SinglePoint %d/%d %d", iCut, max, iEnabledCut);
  if (iCut >= max) {
    // Printf("AAAAA %d/%d %s", iCut, max, cfg["ndmspc"]["process"]["ranges"].dump().c_str());
    // Printf("XXXXXXX %d/%d %s", iCut, max, cfg["ndmspc"]["cuts"].dump().c_str());


    outputFile << ndmscpRunScript << " _macro " << "'\"ndmspc.json\",true' " << inputs << " 'base64|"
               << TBase64::Encode(cfg.dump().c_str()).Data() << "|ndmspc.json'" << std::endl;
    return;
  }

  json cut = cfg["ndmspc"]["cuts"][iCut];

  if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false)
    SinglePoint(iCut + 1, iEnabledCut, max, ndmscpRunScript);

  for (int iBin = 1; iBin <= nBins[iEnabledCut]; iBin += nBinWidths[iEnabledCut]) {
    // Printf("AAAAAAAAA [%d] -> %d %d", iEnabledCut, iBin, iBin + nBinWidths[iEnabledCut] - 1);
    cfg["ndmspc"]["process"]["ranges"][iEnabledCut] = {iBin, iBin + nBinWidths[iEnabledCut] - 1};
    SinglePoint(iCut + 1, iEnabledCut + 1, max, ndmscpRunScript);
  }
}

int NdmspcGenerateJobs(std::string configFileName = "myAnalysis.json",
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

  outputFile.open(outputFileName.c_str());
  if (!outputFile.is_open()) {
    Printf("Error: Problem opening output file '%s' !!!", outputFileName.c_str());
    return 2;
  }

  for (auto & ji : cfg["ndmspc"]["job"]["inputs"]) {
    inputs += "'" + ji.get<std::string>() + "' ";
  }

  // TODO nBins - read it from input file file

  int rebin = 1;
  int i     = 0;
  for (auto & cut : cfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;

    if (cut["rebin"].is_number_integer())
      rebin = cut["rebin"].get<int>();
    else
      rebin = 1;
    nBins[i] = nBins[i] / rebin;
    i++;
  }
  // return 1;
  int cutSize = cfg["ndmspc"]["cuts"].size();


  // return 1;

  json dataHistogramBins = cfg["ndmspc"]["data"]["histogram"]["bins"];
  for (auto & dhb : dataHistogramBins) {
    cfg["ndmspc"]["data"]["histogram"]["bins"] = json::array();
    cfg["ndmspc"]["data"]["histogram"]["bins"].push_back(dhb);
    Printf("%s", cfg["ndmspc"]["data"]["histogram"]["bins"].dump().c_str());

    SinglePoint(0, 0, cutSize, ndmscpRunScript);
      // Printf("cuts %s", cfg["ndmspc"]["cuts"].dump().c_str());
      // return 1;
  }

  outputFile.close();
  return 0;
}