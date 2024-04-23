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
    "verbose": 0
  },
  "ndmspc": {
    "data": {
      "file": "input.root",
      "objects": ["hNSparse"]
    },
    "cuts": [{"enabled": false, "axis": "hNSparseAxisName", "min":3, "max": 3, "rebin":1}],
    "result": {
      "names": []
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
TList * NdmspcProcess(TList * inputList, json cfg, THnSparse * finalResults, Int_t * point)
{
  TList *outputList = new TList();
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

  if (!gROOT->IsBatch()) {
    h->DrawCopy();
  }

  outputList->Add(h);
  return outputList;
}
)"""";

int NdmspcInit(std::string inFile = "data.root", std::string inHnSparseName = "phianalysis-t-hn-sparse_std/unlike",
               std::string inCutAxisName = "axis1-pt")
{
  cfg["ndmspc"]["data"]["file"]    = inFile.c_str();
  cfg["ndmspc"]["data"]["objects"] = {inHnSparseName.c_str()};
  cfg["ndmspc"]["cuts"][0]["axis"] = inCutAxisName.c_str();

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
