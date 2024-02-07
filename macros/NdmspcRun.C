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
  "ndmspc": {
    "verbose": 0,
    "result": {
      "names": ["p1", "p2"]
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "out.root",
      "opt": "?remote=1"
    }
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
  ndmspcBase.merge_patch(cfg);
  cfg = ndmspcBase;

  if (!configFile.IsNull() && !gSystem->AccessPathName(gSystem->ExpandPathName(configFile.Data()))) {
    std::ifstream f(configFile);
    json          cfgJson = json::parse(f);
    cfg.merge_patch(cfgJson);
  }

  if (!cfg["ndmspc"]["verbose"].is_null() && cfg["ndmspc"]["verbose"].is_number_integer())
    _ndmspcVerbose = cfg["ndmspc"]["verbose"].get<int>();

  if (show) Printf("%s", cfg.dump(2).c_str());

  // handle specific options
  if (cfg["ndmspc"]["file"]["cache"].is_string() && !cfg["ndmspc"]["file"]["cache"].get<std::string>().empty())
    TFile::SetCacheFileDir(gSystem->ExpandPathName(cfg["ndmspc"]["file"]["cache"].get<std::string>().c_str()), 1, 1);

  TH1::AddDirectory(kFALSE);
}

TList * NdmspcInit(json cfg)
{

  if (_currentInputFile && _currentInputObjects) return _currentInputObjects;

  if (cfg["ndmspc"]["data"]["file"].get<std::string>().empty()) {
    Printf("Error: Input file is empty !!! Aborting ...");
    return nullptr;
  }
  if (_ndmspcVerbose >= 1) Printf("Opening file '%s' ...", cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
  _currentInputFile = TFile::Open(cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
  if (!_currentInputFile) {
    Printf("Error: Cannot open file '%s' !", cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
    return nullptr;
  }

  if (!_currentInputObjects) _currentInputObjects = new TList();

  THnSparse *s, *stmp;
  for (auto & obj : cfg["ndmspc"]["data"]["objects"]) {
    if (_ndmspcVerbose >= 3) Printf("obj=%s", obj.get<std::string>().c_str());
    if (obj.get<std::string>().empty()) continue;
    std::stringstream src(obj.get<std::string>().c_str());

    std::string item;
    s = nullptr;
    while (getline(src, item, '+')) {
      if (s == nullptr)
        s = (THnSparse *)_currentInputFile->Get(item.c_str());
      else {
        stmp = (THnSparse *)_currentInputFile->Get(item.c_str());
        if (stmp == nullptr) {
          Printf("Error: Cannot open object '%s' !!!", item.c_str());
          continue;
        }
        s->Add(stmp);
      }
    }
    if (!s) {
      Printf("Error : Could not open '%s' from file '%s' !!! Skipping ...", obj.get<std::string>().c_str(),
             cfg["data"]["file"].get<std::string>().c_str());
      return nullptr;
    }
    _currentInputObjects->Add(s);
  }

  if (_currentInputObjects->GetEntries() != cfg["ndmspc"]["data"]["objects"].size()) {
    Printf("Error : Could not open all requested objects !!! Aborting ...");
  }

  return _currentInputObjects;
}

void NdmspcBins(int & min, int & max, int rebin = 1)
{
  int binMin  = min;
  int binMax  = max;
  int binDiff = binMax - binMin;

  if (rebin > 1) {
    binMin = 1 + ((binMin - 1) * rebin);
    binMax = ((binMin - 1) + rebin * (binDiff + 1));
  }
  min = binMin;
  max = binMax;
}

bool NdmspcApplyCuts(json & cfg)
{
  TString     titlePostfix = "";
  THnSparse * s;

  int iCut  = 0;
  int rebin = 1;

  _currentPoint[iCut] = 0;
  for (size_t i = 0; i < _currentInputObjects->GetEntries(); i++) {
    s    = (THnSparse *)_currentInputObjects->At(i);
    iCut = 1;
    for (auto & cut : cfg["ndmspc"]["cuts"]) {

      if (cut.is_null()) continue;

      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;

      if (cut["bin"]["rebin"].is_number_integer()) rebin = cut["bin"]["rebin"].get<int>();

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

      int binMin = cut["bin"]["min"].get<int>();
      int binMax = cut["bin"]["max"].get<int>();

      NdmspcBins(binMin, binMax, rebin);

      s->GetAxis(id)->SetRange(binMin, binMax);

      _currentPoint[iCut] = cut["bin"]["min"].get<int>();

      if (i == 0)
        titlePostfix += TString::Format("%s[%.2f,%.2f] ", s->GetAxis(id)->GetName(),
                                        s->GetAxis(id)->GetBinLowEdge(binMin), s->GetAxis(id)->GetBinUpEdge(binMax));

      iCut++;
    }
  }
  if (!titlePostfix.IsNull()) {
    titlePostfix.Remove(titlePostfix.Length() - 1);
    Printf("Processing '%s' ...", titlePostfix.Data());
    cfg["ndmspc"]["projection"]["title"] = titlePostfix.Data();
  }

  return true;
}

void NdmspcSaveFile(TList * outputList, json cfg)
{

  TString outputFileName;
  if (!cfg["ndmspc"]["output"]["host"].get<std::string>().empty())
    outputFileName += cfg["ndmspc"]["output"]["host"].get<std::string>().c_str();
  if (!cfg["ndmspc"]["output"]["dir"].get<std::string>().empty())
    outputFileName += cfg["ndmspc"]["output"]["dir"].get<std::string>().c_str();
  if (cfg["ndmspc"]["cuts"].is_array() && !outputFileName.IsNull())
    for (auto & cut : cfg["ndmspc"]["cuts"]) {
      outputFileName += TString::Format("/%d", cut["bin"]["min"].get<int>());
    }
  if (!outputFileName.IsNull()) outputFileName += "/";
  if (!cfg["ndmspc"]["output"]["file"].get<std::string>().empty())
    outputFileName += cfg["ndmspc"]["output"]["file"].get<std::string>().c_str();

  TFile * fOut = TFile::Open(
      TString::Format("%s%s", outputFileName.Data(), cfg["ndmspc"]["output"]["opt"].get<std::string>().c_str()).Data(),
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
  const int                ndims = cfg["ndmspc"]["cuts"].size() + 1;
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
  for (auto & cut : cfg["ndmspc"]["cuts"]) {
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

  if (!cfg["ndmspc"]["result"]["names"].is_null() && cfg["ndmspc"]["result"]["names"].is_array()) {
    i = 1;
    for (auto & n : cfg["ndmspc"]["result"]["names"]) {
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
  else {
    if (_ndmspcVerbose >= 0) Printf("Skipping ...");
    }

  return true;
}

bool NdmspcProcessRecursive(int i, json & cfg)
{
  if (i < 0) {
    return NdmspcProcessSinglePoint(cfg);
  }

  THnSparse * s = (THnSparse *)_currentInputObjects->At(0);
  TAxis *     a = (TAxis *)s->GetListOfAxes()->FindObject(cfg["ndmspc"]["cuts"][i]["axis"].get<std::string>().c_str());
  if (a == nullptr) return false;
  Int_t start = 1;
  Int_t end   = a->GetNbins();
  int   rebin = 1;
  if (cfg["ndmspc"]["cuts"][i]["bin"]["rebin"].is_number_integer())
    rebin = cfg["ndmspc"]["cuts"][i]["bin"]["rebin"].get<int>();

  if (rebin > 1) end /= rebin;

  if (cfg["process"]["ranges"].is_array()) {
    start = cfg["ndmspc"]["process"]["ranges"][i][0];
    if (cfg["ndmspc"]["process"]["ranges"][i][1] < end) end = cfg["ndmspc"]["process"]["ranges"][i][1];
  }

  for (Int_t iBin = start; iBin <= end; iBin++) {
    int binMin = iBin;
    int binMax = iBin;
    cfg["ndmspc"]["cuts"][i]["bin"]["min"] = binMin;
    cfg["ndmspc"]["cuts"][i]["bin"]["max"] = binMax;
    NdmspcProcessRecursive(i - 1, cfg);
  }
  return true;
}

int NdmspcRun(TString cfgFile = "", bool showConfig = false)
{

  NdmspcLoadConfig(cfgFile.Data(), cfg, showConfig);

  std::string type;
  if (cfg["ndmspc"]["process"]["type"].is_string()) type = cfg["ndmspc"]["process"]["type"].get<std::string>();

  if (type.empty()) {
    Printf("Warning: [ndmspc][process][type] is missing or is empty in configuration !!! Setting it ot 'single' ...");
    type                             = "single";
    cfg["ndmspc"]["process"]["type"] = type;
  }

  TList * inputList = NdmspcInit(cfg);
  if (inputList == nullptr) return 1;

  if (!type.compare("single")) {
    if (!NdmspcProcessSinglePoint(cfg)) return 3;
  }
  else if (!type.compare("all")) {
    NdmspcProcessRecursive(cfg["ndmspc"]["cuts"].size() - 1, cfg);
  }
  else {
    Printf("Error: Value [process][type]='%s' is not supported !!! Exiting ...", type.c_str());
    return 4;
  }
  NdmspcDestroy();
  return 0;
}
