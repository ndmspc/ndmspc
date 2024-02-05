#include <TFile.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "NdmspcMacro.C"

using json = nlohmann::json;

json    cfg;
void    NdmspcDefaultConfig(json & cfg);
TList * NdmspcProcess(TList * inputList = nullptr, json cfg = R"()"_json, THnSparse * finalResults = nullptr,
                      Int_t * point = nullptr);

json ndmspcBase = R"({
  "output": {
    "host": "",
    "dir": "",
    "file": "out.root",
    "opt": "?remote=1"
  },
  "ndmspc": {
    "verbose": 0
  },
  "verbose" : 0

})"_json;

int         _ndmspcVerbose       = 0;
TFile *     _currentInputFile    = nullptr;
TList *     _currentInputObjects = nullptr;
THnSparse * _finalResults        = nullptr;
Int_t       _currentPoint[10];

void NdmspcLoadConfig(TString configFile, json & cfg, bool show = true)
{
  NdmspcDefaultConfig(cfg);
  cfg.merge_patch(ndmspcBase);

  if (!configFile.IsNull() && !gSystem->AccessPathName(gSystem->ExpandPathName(configFile.Data()))) {
    std::ifstream f(configFile);
    json          cfgJson = json::parse(f);
    cfg.merge_patch(cfgJson);
  }

  if (!cfg["ndmspc"]["verbose"].is_null() && cfg["ndmspc"]["verbose"].is_number_integer())
    _ndmspcVerbose = cfg["ndmspc"]["verbose"].get<int>();

  int verbose = 0;
  if (!cfg["verbose"].is_null() && cfg["verbose"].is_number_integer()) {
    verbose = cfg["verbose"].get<int>();
  }

  if (verbose >= 1) Printf("%s", cfg.dump(2).c_str());

  // handle specific options
  if (cfg["file"]["cache"].is_string() && !cfg["file"]["cache"].get<std::string>().empty())
    TFile::SetCacheFileDir(gSystem->ExpandPathName(cfg["file"]["cache"].get<std::string>().c_str()), 1, 1);

  TH1::AddDirectory(kFALSE);
}

TList * NdmspcInit(json cfg)
{

  if (_currentInputFile && _currentInputObjects) return _currentInputObjects;

  if (cfg["data"]["file"].get<std::string>().empty()) {
    Printf("Error: Input file is empty !!! Aborting ...");
    return nullptr;
  }
  if (_ndmspcVerbose >= 2) Printf("Opening file '%s' ...", cfg["data"]["file"].get<std::string>().c_str());
  _currentInputFile = TFile::Open(cfg["data"]["file"].get<std::string>().c_str());
  if (!_currentInputFile) {
    Printf("Error: Cannot open file '%s' !", cfg["data"]["file"].get<std::string>().c_str());
    return nullptr;
  }

  if (!_currentInputObjects) _currentInputObjects = new TList();

  TObject * o;
  for (auto & obj : cfg["data"]["objects"]) {
    if (_ndmspcVerbose >= 3) Printf("obj=%s", obj.get<std::string>().c_str());
    if (!obj.get<std::string>().empty()) {
      o = _currentInputFile->Get(obj.get<std::string>().c_str());
      if (!o) {
        Printf("Error : Could not open '%s' from file '%s' !!! Skipping ...", obj.get<std::string>().c_str(),
               cfg["data"]["file"].get<std::string>().c_str());
        return nullptr;
      }
      _currentInputObjects->Add(o);
    }
    else {
      _currentInputObjects->Add(new TNamed("empty", "Empty object"));
    }
  }

  if (_currentInputObjects->GetEntries() != cfg["data"]["objects"].size()) {
    Printf("Error : Could not open all requested objects !!! Aborting ...");
  }

  return _currentInputObjects;
}

bool NdmspcApplyCuts(json & cfg)
{
  TString     titlePostfix = "";
  THnSparse * s;

  int iCut            = 0;
  _currentPoint[iCut] = 0;
  for (size_t i = 0; i < _currentInputObjects->GetEntries(); i++) {
    s    = (THnSparse *)_currentInputObjects->At(i);
    iCut = 1;
    for (auto & cut : cfg["cuts"]) {

      if (cut.is_null()) continue;

      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;

      if (cut["axis"].is_string() && cut["axis"].get<std::string>().empty()) {
        std::cerr << "Error: Axis name is empty ('" << cut << "') !!! Exiting ..." << std::endl;
        return false;
      }
      if (cut["bin"]["min"].get<int>() < 0 || cut["bin"]["max"].get<int>() < 0) {
        std::cerr << "Error: Bin min or max is less then 0 ('" << cut << "') !!! Exiting ..." << std::endl;
        return false;
      }

      Int_t id = s->GetListOfAxes()->IndexOf(s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str()));
      if (id == s->GetListOfAxes()->GetEntries()) {
        Printf("Axis '%s' was not found !!! Skipping ...", cut["axis"].get<std::string>().c_str());
        return false;
      }
      s->GetAxis(id)->SetRange(cut["bin"]["min"].get<int>(), cut["bin"]["max"].get<int>());
      _currentPoint[iCut] = cut["bin"]["min"].get<int>();

      if (i == 0)
        titlePostfix += TString::Format("%s[%.2f,%.2f] ", s->GetAxis(id)->GetName(),
                                        s->GetAxis(id)->GetBinLowEdge(cut["bin"]["min"].get<int>()),
                                        s->GetAxis(id)->GetBinUpEdge(cut["bin"]["max"].get<int>()));

      iCut++;
    }
  }
  if (!titlePostfix.IsNull()) {
    titlePostfix.Remove(titlePostfix.Length() - 1);
    Printf("Processing '%s' ...", titlePostfix.Data());
    cfg["projection"]["title"] = titlePostfix.Data();
  }

  return true;
}

void NdmspcSaveFile(TList * outputList, json cfg)
{

  TString outputFileName;
  if (!cfg["output"]["host"].get<std::string>().empty())
    outputFileName += cfg["output"]["host"].get<std::string>().c_str();
  if (!cfg["output"]["dir"].get<std::string>().empty())
    outputFileName += cfg["output"]["dir"].get<std::string>().c_str();
  if (cfg["cuts"].is_array() && !outputFileName.IsNull())
    for (auto & cut : cfg["cuts"]) {
      outputFileName += TString::Format("/%d", cut["bin"]["min"].get<int>());
    }
  if (!outputFileName.IsNull()) outputFileName += "/";
  if (!cfg["output"]["file"].get<std::string>().empty())
    outputFileName += cfg["output"]["file"].get<std::string>().c_str();

  TFile * fOut = TFile::Open(
      TString::Format("%s%s", outputFileName.Data(), cfg["output"]["opt"].get<std::string>().c_str()).Data(),
      "RECREATE");
  outputList->Write();
  fOut->Close();

  if (_ndmspcVerbose >= 0) Printf("Objects stored in '%s'", outputFileName.Data());
}
void NdmspcDestroy()
{

  if (_currentInputObjects) {
    _currentInputObjects->Clear();
    delete _currentInputObjects;
    _currentInputObjects = nullptr;
  }

  if (_currentInputFile) {
    _currentInputFile->Close();
    delete _currentInputFile;
    _currentInputFile = nullptr;
  }
}

THnSparse * CreateResult(json & cfg)
{

  THnSparse *              s     = (THnSparse *)_currentInputObjects->At(0);
  const int                ndims = cfg["cuts"].size() + 1;
  Int_t                    bins[ndims];
  Double_t                 xmin[ndims];
  Double_t                 xmax[ndims];
  std::vector<std::string> names;
  std::vector<std::string> titles;

  bins[0] = 10;
  xmin[0] = 0;
  xmax[0] = 10;
  names.push_back("params");
  titles.push_back("Parameters");

  int i = 1;
  for (auto & cut : cfg["cuts"]) {
    if (_ndmspcVerbose >= 3) std::cout << "CreateResult() : " << cut.dump() << std::endl;
    TAxis * a = (TAxis *)s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str());
    if (a == nullptr) return nullptr;
    bins[i] = a->GetNbins();
    xmin[i] = a->GetXmin();
    xmax[i] = a->GetXmax();
    names.push_back(a->GetName());
    titles.push_back(a->GetTitle());
    i++;
  }

  THnSparse * fres = new THnSparseD("results", "Final results", ndims, bins, xmin, xmax);
  i                = 0;
  for (auto & n : names) {
    fres->GetAxis(i)->SetNameTitle(names.at(i).c_str(), titles.at(i).c_str());
    i++;
  }

  if (!cfg["result"]["names"].is_null() && cfg["result"]["names"].is_array()) {
    i = 1;
    for (auto & n : cfg["result"]["names"]) {
      TAxis * a = fres->GetAxis(0);
      a->SetBinLabel(i++, n.get<std::string>().c_str());
    }
  }

  if (_ndmspcVerbose >= 3) fres->Print("all");

  return fres;
}

bool NdmspcProcessSinglePoint(json & cfg)
{
  if (!NdmspcApplyCuts(cfg)) return false;

  if (_finalResults == nullptr) delete _finalResults;
  _finalResults = CreateResult(cfg);

  TList * outputList = NdmspcProcess(_currentInputObjects, cfg, _finalResults, _currentPoint);

  if (outputList) NdmspcSaveFile(outputList, cfg);

  return true;
}

bool NdmspcProcessRecursive(int i, json & cfg)
{
  if (i < 0) {
    return NdmspcProcessSinglePoint(cfg);
  }

  THnSparse * s = (THnSparse *)_currentInputObjects->At(0);
  TAxis *     a = (TAxis *)s->GetListOfAxes()->FindObject(cfg["cuts"][i]["axis"].get<std::string>().c_str());
  if (a == nullptr) return false;
  Int_t start = 1;
  Int_t end   = a->GetNbins();

  if (cfg["process"]["ranges"].is_array()) {
    start = cfg["process"]["ranges"][i][0];
    end   = cfg["process"]["ranges"][i][1];
  }

  for (Int_t iBin = start; iBin <= end; iBin++) {
    cfg["cuts"][i]["bin"]["min"] = iBin;
    cfg["cuts"][i]["bin"]["max"] = iBin;
    NdmspcProcessRecursive(i - 1, cfg);
  }
  return true;
}

int NdmspcRun(TString cfgFile = "", bool showConfig = true)
{

  NdmspcLoadConfig(cfgFile.Data(), cfg, showConfig);

  std::string type;
  if (cfg["process"]["type"].is_string()) type = cfg["process"]["type"].get<std::string>();

  if (type.empty()) {
    Printf("Warning: [process][type] is missing or is empty in configuration !!! Setting it ot 'single' ...");
    type                   = "single";
    cfg["process"]["type"] = type;
  }

  TList * inputList = NdmspcInit(cfg);
  if (inputList == nullptr) return 1;

  if (!type.compare("single")) {
    if (!NdmspcProcessSinglePoint(cfg)) return 3;
  }
  else if (!type.compare("all")) {
    NdmspcProcessRecursive(cfg["cuts"].size() - 1, cfg);
  }
  else {
    Printf("Error: Value [process][type]='%s' is not supported !!! Exiting ...", type.c_str());
    return 4;
  }
  NdmspcDestroy();
  return 0;
}
