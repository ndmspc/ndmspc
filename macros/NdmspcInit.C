#include <TFile.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>

#include <fstream>
#include <nlohmann/json.hpp>


using json = nlohmann::json;


json cfg   = R"({

  "user": {
    "proj": 0,
    "minEntries": 1,
    "verbose": 0
  },
  "ndmspc": {
    "data": {
      "file": "input.root",
      "objects": ["hNSparse"]
    },
    "cuts": [],
    "result": {
      "parameters": { "labels": ["Integral"], "default": "Integral" }
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "content.root",
      "opt": "?remote=1"
    },
    "process": {
      "type": "single"
    },
    "log": {
      "type": "error-only",
      "dir": "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/logs"
    },
    "job":{
      "inputs": []
    },
    "verbose": 0
  }
})"_json;


std::string macroTemplateHeader = R""""(
#include <TCanvas.h>
#include <TF1.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TROOT.h>
#include <TString.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

void NdmspcDefaultConfig(json & cfg)
{
)"""";

std::string macroTemplate = R""""(
}
int SetResultValueError(THnSparse * finalResults, std::string name, Int_t * point, double val, double err);
bool NdmspcProcess(TList * inputList, json cfg, THnSparse * finalResults, Int_t * point,
                   std::vector<std::string> pointLabels, json pointValue, TList * outputList)
{
  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }

  if (inputList == nullptr) return false;

  THnSparse * hs = (THnSparse *)inputList->At(0);

  int projId = cfg["user"]["proj"].get<int>();
  TH1D *      h  = hs->Projection(projId, "O");
  
  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();  
  h->SetNameTitle("h", TString::Format("h - %s", titlePostfix.Data()).Data());
  outputList->Add(h);

  // Skip bin (do not save any output)
  if (h->GetEntries() < cfg["user"]["minEntries"].get<int>()) 
    return false; 

  Double_t integral = h->Integral();
  if (verbose >= 0)
    Printf("Integral = %f ", integral);


  if (finalResults) {
    SetResultValueError(finalResults, "Integral", point, integral, TMath::Sqrt(integral));
  }

  if (!gROOT->IsBatch() && !cfg["ndmspc"]["process"]["type"].get<std::string>().compare("single")) {
    h->DrawCopy();
  }


  return true;
}

int SetResultValueError(THnSparse * finalResults, std::string name, Int_t * point, double val, double err)
{
  int idx = finalResults->GetAxis(0)->FindBin(name.c_str());
  if (idx <= 0) {
    return idx;
  }
  point[0]       = idx;
  Long64_t binId = finalResults->GetBin(point);
  finalResults->SetBinContent(binId, val);
  finalResults->SetBinError(binId, err);

  return idx;
}
)"""";

int NdmspcInit(std::string inFile = "data.root", std::string inHnSparseName = "phianalysis-t-hn-sparse_std/unlike")
{
  cfg["ndmspc"]["data"]["file"]    = inFile.c_str();
  cfg["ndmspc"]["data"]["objects"] = {inHnSparseName.c_str()};

  std::string   outputMacro = "NdmspcPointMacro.C";
  std::ofstream file(outputMacro.c_str());
  file << macroTemplateHeader.c_str();
  file << "  cfg = R\"(" << std::endl;
  file << std::setw(2) << cfg << std::endl;
  file << ")\"_json;" << std::endl;

  file << macroTemplate.c_str();

  Printf("File '%s' was generated ...", outputMacro.c_str());
  return 0;
}
