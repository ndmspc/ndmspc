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
std::vector<int> nBins;
std::vector<int> nBinWidths;

void SinglePoint(int iCut, int iEnabledCut, int max, std::string & ndmscpRunScript)
{
  // Printf("SinglePoint %d/%d %d", iCut, max, iEnabledCut);
  if (iCut >= max) {
    Printf("Job ranges : %s", cfg["ndmspc"]["process"]["ranges"].dump().c_str());
    // Printf("XXXXXXX %d/%d %s", iCut, max, cfg["ndmspc"]["cuts"].dump().c_str());

    outputFile << ndmscpRunScript << " _macro " << "'\"ndmspc.json\",true' " << inputs << " 'base64|"
               << TBase64::Encode(cfg.dump().c_str()).Data() << "|ndmspc.json'" << std::endl;
    return;
  }

  json cut = cfg["ndmspc"]["cuts"][iCut];

  if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) {
    SinglePoint(iCut + 1, iEnabledCut, max, ndmscpRunScript);
    return;
  }

  for (int iBin = 1; iBin <= nBins[iEnabledCut]; iBin += nBinWidths[iEnabledCut]) {
    // Printf("AAAAAAAAA [%d] -> %d %d", iEnabledCut, iBin, iBin + nBinWidths[iEnabledCut] - 1);

    int m = iBin + nBinWidths[iEnabledCut] - 1;
    if (m > nBins[iEnabledCut]) m = nBins[iEnabledCut];
    cfg["ndmspc"]["process"]["ranges"][iEnabledCut] = {iBin, m};
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
  int cutSize = cfg["ndmspc"]["cuts"].size();

  if (!cfg["ndmspc"]["process"]["split"].is_null() && cfg["ndmspc"]["process"]["split"].is_array()) {
    for (auto & w : cfg["ndmspc"]["process"]["split"]) {
      nBinWidths.push_back(w.get<int>());
    }
  }
  else {
    nBinWidths = {2, 10, 10};
  }
  // return 1;

  json dataHistogramBins = cfg["ndmspc"]["data"]["histogram"]["bins"];
  if (!cfg["ndmspc"]["data"]["histogram"].is_null() && cfg["ndmspc"]["data"]["histogram"]["enabled"].get<bool>()) {

    for (auto & dhb : dataHistogramBins) {

      if (nBins.size() == 0) {
        std::string p = cfg["ndmspc"]["data"]["histogram"]["base"].get<std::string>();
        for (auto & b : dhb) {
          p += "/" + std::to_string(b.get<int>());
        }
        p += "/" + cfg["ndmspc"]["data"]["histogram"]["filename"].get<std::string>();
        Printf("%s", p.c_str());

        TFile * fIn = TFile::Open(p.c_str());

        std::string objName = cfg["ndmspc"]["data"]["directory"].get<std::string>();
        objName += "/unlikepm";
        int i = 0;
        for (auto & cut : cfg["ndmspc"]["cuts"]) {
          int rebin = 1;
          if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
          THnSparse * s = (THnSparse *)fIn->Get(objName.c_str());
          TAxis *     a = (TAxis *)s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str());
          a->Print();

          if (cut["rebin"].is_number_integer())
            rebin = cut["rebin"].get<int>();
          else
            rebin = 1;
          Printf("%d %d %d %d", nBinWidths[i] >= a->GetNbins(), nBinWidths[i], a->GetNbins(), a->GetNbins() / rebin);
          if (nBinWidths[i] >= a->GetNbins()) {
            nBins.push_back(a->GetNbins());
            nBinWidths[0] = a->GetNbins();
          }
          else {
            nBins.push_back(a->GetNbins() / rebin);
          }

          i++;
        }
      }
      // exit(1);

      Printf("%d(%d) %d(%d)", nBins[0], nBinWidths[0], nBins[1], nBinWidths[1]);
      // }

      // for (auto & dhb : dataHistogramBins) {
      cfg["ndmspc"]["data"]["histogram"]["bins"] = json::array();
      cfg["ndmspc"]["data"]["histogram"]["bins"].push_back(dhb);
      Printf("%s", cfg["ndmspc"]["data"]["histogram"]["bins"].dump().c_str());

      SinglePoint(0, 0, cutSize, ndmscpRunScript);
      // Printf("cuts %s", cfg["ndmspc"]["cuts"].dump().c_str());
      // return 1;
    }
  }
  else {
    SinglePoint(0, 0, cutSize, ndmscpRunScript);
  }

  outputFile.close();
  return 0;
}