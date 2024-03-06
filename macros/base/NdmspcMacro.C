#include <TCanvas.h>
#include <TF1.h>
#include <TROOT.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
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
      "names": ["param0"]
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "out.root",
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
        "https://gitlab.com/ndmspc/ndmspc/-/raw/main/macros/base/NdmspcMacro.C|NdmspcMacro.C"
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

  THnSparse * sigBgSparse = (THnSparse *)inputList->At(0);
  TH1D *h = sigBgSparse->Projection(projId);
  h->SetName("h");

  TList * outputList = new TList();
  Long64_t binid;
  if (finalResults) {
    outputList->Add(finalResults);
    for (auto & name : cfg["ndmspc"]["result"]["names"]) {
      std::string n = name.get<std::string>();
      if (!n.compare("param0")) {
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
