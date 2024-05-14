#include <TCanvas.h>
#include <TF1.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TROOT.h>
#include <TString.h>
// #include <TStyle.h>
// #include "NdmspcUtils.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

void NdmspcDefaultConfig(json & cfg)
{
  cfg = R"({
  "user": {
    "proj": 0,
    "verbose": 0
  },
  "ndmspc": {
    "data": {
      "file": "root://eos.ndmspc.io//eos/ndmspc/scratch/jurban/out.root",
      "objects": ["hs"]
    },
    "cuts": [{"enabled": true,"axis": "axis1", "rebin": 1,"bin": {"min": 3 ,"max": 3}}],
    "result": {
      "names": ["p0"]
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
      "type": "always",
      "dir": "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/logs"
    },
    "job":{
      "inputs": [
        "https://gitlab.com/ndmspc/ndmspc/-/raw/main/macros/base/NdmspcPointMacro.C|NdmspcPointMacro.C"
      ]
    },
    "verbose": 0
  }
})"_json;
}

TList * NdmspcProcess(TList * inputList, json cfg, THnSparse * finalResults, Int_t * point)
{

  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }

  if (inputList == nullptr) return nullptr;
  int projId = cfg["user"]["proj"].get<int>();

  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();

  THnSparse * hs = (THnSparse *)inputList->At(0);
  TH1D *      h  = hs->Projection(projId, "O");
  h->SetNameTitle("h", TString::Format("h %s", titlePostfix.Data()).Data());

  TList *  outputList = new TList();
  Long64_t binid;
  if (finalResults) {
    outputList->Add(finalResults);
    for (auto & name : cfg["ndmspc"]["result"]["parameters"]["labels"]) {
      std::string n = name.get<std::string>();
      if (!n.compare("p0")) {
        point[0] = 0;
        binid    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, h->Integral());
        finalResults->SetBinError(binid, TMath::Sqrt(h->Integral()));
      }
    }
  }

  outputList->Add(h);
  return outputList;
}
