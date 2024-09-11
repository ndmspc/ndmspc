#include <TFile.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// #include "NdmspcPointMacro.C"

json cfg;
void NdmspcDefaultConfig(json & cfg);
bool NdmspcPointMacro(TList * inputList, json cfg, THnSparse * finalResults, Int_t * point,
                      std::vector<std::string> pointLabels, json pointValue, TList * outputList, bool & skipCurrentBin,
                      bool & processExit);

json ndmspcBase = R"({
  "ndmspc": {
    "verbose": 0,
    "result": {
      "parameters": { "labels": ["p0", "p1"], "default": 0 }
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "content.root",
      "delete": "onInit",
      "opt": "?remote=1"
    }
  },
  "verbose" : 0
})"_json;

int                      _ndmspcVerbose                = 0;
int                      _currentBinCount              = 0;
THnSparse *              _currentProccessHistogram     = nullptr;
std::vector<TAxis *>     _currentProcessHistogramAxes  = {};
std::vector<Int_t>       _currentProcessHistogramPoint = {};
TFile *                  _currentInputFile             = nullptr;
TList *                  _currentInputObjects          = nullptr;
TFile *                  _currentOutputFile            = nullptr;
std::string              _currentOutputFileName        = {};
TDirectory *             _currentOutputRootDirectory   = nullptr;
TH1S *                   _mapAxesType                  = nullptr;
THnSparse *              _finalResults                 = nullptr;
Int_t                    _currentPoint[20];
std::vector<std::string> _currentPointLabels;
json                     _currentPointValue;
bool                     _currentProcessOk   = false;
bool                     _currentSkipBin     = false;
bool                     _currentProcessExit = false;
bool                     NdmspcLoadConfig(TString configFile, json & cfg, bool show = true, TString ouputConfig = "")
{
  NdmspcDefaultConfig(cfg);
  ndmspcBase.merge_patch(cfg);
  cfg = ndmspcBase;

  if (!configFile.IsNull() && gSystem->AccessPathName(gSystem->ExpandPathName(configFile.Data()))) {
    Printf("Error: Config file '%s' was not found !!!", configFile.Data());
    return false;
  }
  if (!configFile.IsNull()) {
    std::ifstream f(configFile);
    if (f.is_open()) {
      json cfgJson = json::parse(f);
      cfg.merge_patch(cfgJson);
      Printf("Using config file '%s' ...", gSystem->ExpandPathName(configFile.Data()));
    }
    else {
      Printf("Error: Problem opening file '%s' !!!", configFile.Data());
      return false;
    }
  }

  if (!cfg["ndmspc"]["verbose"].is_null() && cfg["ndmspc"]["verbose"].is_number_integer())
    _ndmspcVerbose = cfg["ndmspc"]["verbose"].get<Int_t>();

  if (show) Printf("%s", cfg.dump(2).c_str());

  if (!ouputConfig.IsNull()) {
    std::ofstream file(ouputConfig.Data());
    file << cfg;
    Printf("Config saved to file '%s' ...", ouputConfig.Data());
    return false;
  }

  // handle specific options
  // if (cfg["ndmspc"]["file"]["cache"].is_string() && !cfg["ndmspc"]["file"]["cache"].get<std::string>().empty())
  //   TFile::SetCacheFileDir(gSystem->ExpandPathName(cfg["ndmspc"]["file"]["cache"].get<std::string>().c_str()), 1, 1);

  TH1::AddDirectory(kFALSE);
  return true;
}

TList * NdmspcInit(json cfg)
{

  if (_ndmspcVerbose >= 2) Printf("NdmspcInit ...");

  if (_currentInputFile && _currentInputObjects) return _currentInputObjects;

  if (cfg["ndmspc"]["data"]["file"].get<std::string>().empty()) {
    Printf("Error: Input file is empty !!! Aborting ...");
    return nullptr;
  }
  if (_ndmspcVerbose >= 0) Printf("Opening file '%s' ...", cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
  _currentInputFile = TFile::Open(cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
  if (!_currentInputFile) {
    Printf("Error: Cannot open file '%s' !", cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
    return nullptr;
  }

  if (!_currentInputObjects) _currentInputObjects = new TList();

  THnSparse *s, *stmp;
  for (auto & obj : cfg["ndmspc"]["data"]["objects"]) {
    if (obj.get<std::string>().empty()) continue;

    std::string dirName;
    if (!cfg["ndmspc"]["data"]["directory"].is_null() && cfg["ndmspc"]["data"]["directory"].is_string())
      dirName = cfg["ndmspc"]["data"]["directory"].get<std::string>();

    std::stringstream srcfull(obj.get<std::string>().c_str());

    std::string srcName, sparseName;

    getline(srcfull, srcName, ':');
    if (_ndmspcVerbose >= 2) Printf("srcName=%s", srcName.c_str());
    getline(srcfull, sparseName, ':');
    if (_ndmspcVerbose >= 2) Printf("sparseName=%s", sparseName.c_str());

    std::stringstream src(srcName.c_str());
    std::string       item;

    s = nullptr;
    while (getline(src, item, '+')) {

      std::string objName;
      if (!dirName.empty()) objName = dirName + "/";
      objName += item;
      if (_ndmspcVerbose >= 1) Printf("Opening obj='%s' ...", objName.c_str());
      if (s == nullptr) {

        s = (THnSparse *)_currentInputFile->Get(objName.c_str());
        if (s == nullptr) {
          if (_ndmspcVerbose >= 1) Printf("Warning: Cannot open object '%s' !!!", objName.c_str());
          continue;
        }

        if (s && !sparseName.empty()) s->SetName(sparseName.c_str());
      }
      else {
        if (_ndmspcVerbose >= 1) Printf("Adding obj='%s' ...", objName.c_str());
        stmp = (THnSparse *)_currentInputFile->Get(objName.c_str());
        if (stmp == nullptr) {
          if (_ndmspcVerbose >= 1) Printf("Warning: Cannot open object '%s' !!!", objName.c_str());
          continue;
        }
        if (s) s->Add(stmp);
      }
    }
    if (s) {
      _currentInputObjects->Add(s);
    }
    else {
      if (_ndmspcVerbose >= 1)
        Printf("Warning : Could not open '%s' from file '%s' !!! Skipping ...", obj.get<std::string>().c_str(),
               cfg["ndmspc"]["data"]["file"].get<std::string>().c_str());
      // return nullptr;
    }

    if (_ndmspcVerbose >= 2) Printf("NdmspcInit done ...");
  }

  // if (_currentInputObjects->GetEntries() != cfg["ndmspc"]["data"]["objects"].size()) {
  //   Printf("Error : Could not open all requested objects !!! Aborting ...");
  // }

  // XXXXXXXXXXXXX

  // if (_finalResults) {
  //   int i     = _finalResults->GetNdimensions() - _currentProcessHistogramAxes.size();
  //   int iPIdx = 0;
  //   for (auto & a : _currentProcessHistogramAxes) {
  //     Printf("2 AAAAAAAAAAAAAAAAAAAA i=%d iPIdx=%d %d", i, iPIdx, _currentProcessHistogramPoint[iPIdx]);
  //     _currentPoint[i] = _currentProcessHistogramPoint[iPIdx++];
  //     i++;
  //   }
  // }

  return _currentInputObjects;
}

void NdmspcRebinBins(Int_t & min, Int_t & max, Int_t rebin = 1)
{
  Int_t binMin  = min;
  Int_t binMax  = max;
  Int_t binDiff = binMax - binMin;

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

  Int_t iCut  = 0;
  Int_t rebin = 1;

  _currentPoint[iCut] = 0;
  for (size_t i = 0; i < _currentInputObjects->GetEntries(); i++) {
    s    = (THnSparse *)_currentInputObjects->At(i);
    iCut = 1;
    for (auto & cut : cfg["ndmspc"]["cuts"]) {

      if (cut.is_null()) continue;

      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;

      if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();

      if (cut["axis"].is_string() && cut["axis"].get<std::string>().empty()) {
        std::cerr << "Error: Axis name is empty ('" << cut << "') !!! Exiting ..." << std::endl;
        return false;
      }
      if (cut["bin"]["min"].get<Int_t>() < 0 || cut["bin"]["max"].get<Int_t>() < 0) {
        std::cerr << "Error: Bin min or max is less then 0 ('" << cut << "') !!! Exiting ..." << std::endl;
        return false;
      }

      Int_t id = s->GetListOfAxes()->IndexOf(s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str()));
      if (id == s->GetListOfAxes()->GetEntries()) {
        Printf("Axis '%s' was not found !!! Skipping ...", cut["axis"].get<std::string>().c_str());
        return false;
      }

      Int_t binMin = cut["bin"]["min"].get<Int_t>();
      Int_t binMax = cut["bin"]["max"].get<Int_t>();

      NdmspcRebinBins(binMin, binMax, rebin);

      s->GetAxis(id)->SetRange(binMin, binMax);

      _currentPoint[iCut + _currentProcessHistogramPoint.size()] = cut["bin"]["min"].get<Int_t>();

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

void NdmspcOutputFileOpen(json cfg)
{

  // TString outputFileName;

  _currentOutputFileName = "";

  if (!cfg["ndmspc"]["output"]["host"].get<std::string>().empty())
    _currentOutputFileName += cfg["ndmspc"]["output"]["host"].get<std::string>().c_str();
  if (!cfg["ndmspc"]["output"]["dir"].get<std::string>().empty())
    _currentOutputFileName += cfg["ndmspc"]["output"]["dir"].get<std::string>().c_str();

  if (cfg["ndmspc"]["cuts"].is_array() && !_currentOutputFileName.empty()) {

    std::string axisName;
    // cfgOutput["ndmspc"]["cuts"] = cfg["ndmspc"]["cuts"];
    for (auto & cut : cfg["ndmspc"]["cuts"]) {
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      if (axisName.length() > 0) axisName += "_";
      axisName += cut["axis"].get<std::string>();
    }
    if (axisName.length() > 0) {
      _currentOutputFileName += "/";
      _currentOutputFileName += TString::Format("%s", axisName.c_str());
      _currentOutputFileName += "/";
      _currentOutputFileName += "bins";

      if (cfg["ndmspc"]["output"]["post"].is_string()) {
        std::string post = cfg["ndmspc"]["output"]["post"].get<std::string>();
        if (!post.empty()) {
          // _currentOutputFileName += "/bins/" + post;
          _currentOutputFileName += "/" + post;
        }
      }

      for (auto & cut : cfg["ndmspc"]["cuts"]) {
        if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
        _currentOutputFileName += std::to_string(cut["bin"]["min"].get<Int_t>()) + "/";
      }
    }
  }

  if (!_currentOutputFileName.empty() && _currentOutputFileName[_currentOutputFileName.size() - 1] != '/')
    _currentOutputFileName += "/";

  if (!cfg["ndmspc"]["output"]["file"].get<std::string>().empty())
    _currentOutputFileName += cfg["ndmspc"]["output"]["file"].get<std::string>().c_str();

  _currentOutputFile = TFile::Open(
      TString::Format("%s%s", _currentOutputFileName.c_str(), cfg["ndmspc"]["output"]["opt"].get<std::string>().c_str())
          .Data(),
      "RECREATE");
  // _currentOutputFile->cd();

  _currentOutputFile->mkdir("content");
  _currentOutputRootDirectory = _currentOutputFile->GetDirectory("content");
  _currentOutputRootDirectory->cd();
}

void NdmspcOutputFileClose()
{

  if (_currentOutputFile == nullptr) return;

  if (_ndmspcVerbose >= 2) Printf("Closing file '%s' ...", _currentOutputFileName.c_str());
  _currentOutputRootDirectory->Write();

  _currentOutputFile->cd();
  _finalResults->Write();
  _mapAxesType->Write();
  _currentOutputFile->Close();

  _currentOutputFile          = nullptr;
  _currentOutputRootDirectory = nullptr;

  if (_ndmspcVerbose >= 0) Printf("Objects stored in '%s'", _currentOutputFileName.c_str());
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

  if (_finalResults) return _finalResults;

  THnSparse * s = (THnSparse *)_currentInputObjects->At(0);

  int nDimsParams   = 0;
  int nDimsCuts     = 0;
  int nDimsProccess = _currentProcessHistogramAxes.size();

  for (auto & cut : cfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    nDimsCuts++;
  }

  for (auto & value : cfg["ndmspc"]["result"]["axes"]) {
    nDimsParams++;
  }

  const Int_t              ndims = nDimsCuts + nDimsParams + nDimsProccess + 1;
  Int_t                    bins[ndims];
  Double_t                 xmin[ndims];
  Double_t                 xmax[ndims];
  std::vector<std::string> names;
  std::vector<std::string> titles;
  std::vector<TAxis *>     cutAxes;
  _mapAxesType = new TH1S("mapAxesType", "Type Axes Map", ndims, 0, ndims);
  // _currentOutputRootDirectory->Add

  Int_t nParameters = cfg["ndmspc"]["result"]["parameters"]["labels"].size();
  if (nParameters <= 0) return nullptr;

  Int_t i = 0;

  bins[i] = nParameters;
  xmin[i] = 0;
  xmax[i] = nParameters;
  names.push_back("parameters");
  titles.push_back("parameters");
  _mapAxesType->GetXaxis()->SetBinLabel(i + 1, "par");

  i++;

  // cfg["ndmspc"]["output"]["post"]
  int iTmp = 0;
  for (auto & a : _currentProcessHistogramAxes) {
    // a->Print();
    bins[i] = a->GetNbins();
    xmin[i] = a->GetXmin();
    xmax[i] = a->GetXmax();
    // todo handle variable binning

    names.push_back(a->GetName());
    std::string t = a->GetTitle();
    if (t.empty()) t = a->GetName();
    titles.push_back(t);
    if (iTmp < 2)
      _mapAxesType->GetXaxis()->SetBinLabel(i + 1, "data");
    else
      _mapAxesType->GetXaxis()->SetBinLabel(i + 1, "sys");
    iTmp++;
    i++;
  }

  Int_t rebin = 1;
  for (auto & cut : cfg["ndmspc"]["cuts"]) {
    if (_ndmspcVerbose >= 3) std::cout << "CreateResult() : " << cut.dump() << std::endl;
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;

    TAxis * a = (TAxis *)s->GetListOfAxes()->FindObject(cut["axis"].get<std::string>().c_str());
    if (a == nullptr) return nullptr;

    cutAxes.push_back(a);

    if (cut["rebin"].is_number_integer())
      rebin = cut["rebin"].get<Int_t>();
    else
      rebin = 1;

    bins[i] = a->GetNbins() / rebin;
    xmin[i] = a->GetXmin();
    xmax[i] = a->GetXmax() - (a->GetNbins() % rebin);

    names.push_back(a->GetName());
    std::string t = a->GetTitle();
    if (t.empty()) t = a->GetName();
    titles.push_back(t);
    _mapAxesType->GetXaxis()->SetBinLabel(i + 1, "proj");
    i++;
  }

  for (auto & value : cfg["ndmspc"]["result"]["axes"]) {
    if (!value["labels"].is_null()) {
      bins[i] = value["labels"].size();
      xmin[i] = 0;
      xmax[i] = value["labels"].size();
    }
    else if (!value["ranges"].is_null()) {
      bins[i] = value["ranges"].size();
      xmin[i] = 0;
      xmax[i] = value["ranges"].size();
    }
    else {
      Printf("Error: 'labels' or 'ranges' is missing !!!");
      return nullptr;
    }

    names.push_back(value["name"].get<std::string>().c_str());
    titles.push_back(value["name"].get<std::string>().c_str());
    _mapAxesType->GetXaxis()->SetBinLabel(i + 1, "sys");
    i++;
  }

  _currentPointLabels = names;

  THnSparse * fres = new THnSparseD("results", "Final results", i, bins, xmin, xmax);
  if (!fres) {
    Printf("Error: Could not create 'results' THnSparse object !!!");
    return nullptr;
  }
  int     iAxis = 0;
  TAxis * a     = fres->GetAxis(iAxis);
  if (!a) {
    Printf("Error: 'parameters' axis was not found !!!");
    return nullptr;
  }
  int iLablel = 1;
  for (auto & n : cfg["ndmspc"]["result"]["parameters"]["labels"]) {
    a->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
    a->SetBinLabel(iLablel++, n.get<std::string>().c_str());
  }

  iAxis++;

  // cfg["ndmspc"]["output"]["post"]
  // i         = nDimsCuts + iPar + 1;
  int iLabel;
  int iPIdx = 0;
  for (auto & axis : _currentProcessHistogramAxes) {
    if (axis->GetXbins()->GetArray()) fres->GetAxis(iAxis)->Set(axis->GetNbins(), axis->GetXbins()->GetArray());
    fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());

    for (iLabel = 1; iLabel < _currentProccessHistogram->GetAxis(iPIdx)->GetNbins() + 1; iLabel++) {
      std::string l = _currentProccessHistogram->GetAxis(iPIdx)->GetBinLabel(iLabel);
      if (!l.empty()) fres->GetAxis(iAxis)->SetBinLabel(iLabel, l.c_str());
    }
    _currentPoint[iAxis] = _currentProcessHistogramPoint[iPIdx];
    iPIdx++;
    iAxis++;
  }

  // i = 1;
  for (auto & a : cutAxes) {
    // Printf("%s", )
    if (a->GetXbins()->GetArray()) fres->GetAxis(iAxis)->Set(a->GetNbins(), a->GetXbins()->GetArray());
    fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
    iAxis++;
  }
  int iPar = 0;
  // int iLabel;
  // for (auto & [key, value] : cfg["ndmspc"]["result"].items()) {
  for (auto & value : cfg["ndmspc"]["result"]["axes"]) {
    iPar++;
    iLabel = 1;
    if (!value["labels"].is_null()) {
      for (auto & n : value["labels"]) {
        fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
        fres->GetAxis(iAxis)->SetBinLabel(iLabel++, n.get<std::string>().c_str());
      }
    }
    iLabel = 1;
    if (!value["ranges"].is_null()) {
      for (auto & n : value["ranges"]) {
        fres->GetAxis(iAxis)->SetNameTitle(names.at(iAxis).c_str(), titles.at(iAxis).c_str());
        fres->GetAxis(iAxis)->SetBinLabel(iLabel++, n["name"].get<std::string>().c_str());
      }
    }
    iAxis++;
  }

  if (_ndmspcVerbose >= 1) fres->Print("all");
  // return nullptr;

  return fres;
}

bool NdmspcProcessRecursiveInner(Int_t i, json & cfg, std::vector<std::string> & n)
{
  if (_currentSkipBin) return false;

  if (!_finalResults) {
    _currentProcessExit = true;
    return false;
  }

  if (_currentProcessExit) gSystem->Exit(1);

  if (i < 0) {
    TList * outputList = new TList();

    if (_currentBinCount == 1 && _ndmspcVerbose >= 0) Printf("\t%s", _currentPointValue.dump().c_str());

    bool ok = NdmspcPointMacro(_currentInputObjects, cfg, _finalResults, _currentPoint, _currentPointLabels,
                               _currentPointValue, outputList, _currentSkipBin, _currentProcessExit);
    if (ok && _ndmspcVerbose >= 5) outputList->Print();
    if (ok) {
      _currentProcessOk = true;
    }
    else {
      return false;
    }

    if (_currentOutputFile == nullptr) {
      NdmspcOutputFileOpen(cfg);
    }

    TDirectory * outputDir = _currentOutputRootDirectory;

    int         iPoint = 1;
    std::string path;
    for (int iPoint = 1; iPoint < _finalResults->GetNdimensions(); iPoint++) {
      path += std::to_string(_currentPoint[iPoint]) + "/";
    }

    // if (cfg["ndmspc"]["output"]["post"].is_string()) {
    //   std::string post = cfg["ndmspc"]["output"]["post"].get<std::string>();
    //   if (!post.empty()) {
    //     path += post;
    //   }
    // }

    // Printf("path='%s'", path.c_str());

    _currentOutputRootDirectory->mkdir(path.c_str(), "", true);
    outputDir = _currentOutputRootDirectory->GetDirectory(path.c_str());

    outputDir->cd();
    outputList->Write();
    return true;
  }

  std::string axisName = cfg["ndmspc"]["result"]["axes"][i]["name"].get<std::string>();
  if (!cfg["ndmspc"]["result"]["axes"][i]["labels"].is_null()) {
    for (auto & v : cfg["ndmspc"]["result"]["axes"][i]["labels"]) {
      TAxis * a                    = (TAxis *)_finalResults->GetListOfAxes()->FindObject(axisName.c_str());
      Int_t   id                   = _finalResults->GetListOfAxes()->IndexOf(a);
      _currentPoint[id]            = a->FindBin(v.get<std::string>().c_str());
      _currentPointValue[axisName] = v;
      _currentPointLabels[id]      = v.get<std::string>().c_str();
      NdmspcProcessRecursiveInner(i - 1, cfg, n);
    }
  }
  else if (!cfg["ndmspc"]["result"]["axes"][i]["ranges"].is_null()) {
    for (auto & v : cfg["ndmspc"]["result"]["axes"][i]["ranges"]) {
      TAxis * a                    = (TAxis *)_finalResults->GetListOfAxes()->FindObject(axisName.c_str());
      Int_t   id                   = _finalResults->GetListOfAxes()->IndexOf(a);
      _currentPoint[id]            = a->FindBin(v["name"].get<std::string>().c_str());
      _currentPointValue[axisName] = v;
      _currentPointLabels[id]      = v["name"].get<std::string>().c_str();
      NdmspcProcessRecursiveInner(i - 1, cfg, n);
    }
  }
  else {
    Printf("Error: NdmspcProcessRecursiveInner : No 'labels' or 'ranges' !!!");
    return false;
  }
  return true;
}

bool NdmspcProcessSinglePoint(json & cfg)
{

  _currentProcessOk = false;

  if (!NdmspcApplyCuts(cfg)) return false;

  _currentSkipBin = false;

  if (_finalResults != nullptr) {
    delete _finalResults;
    _finalResults = nullptr;
  }
  _finalResults = CreateResult(cfg);

  json resultAxes = cfg["ndmspc"]["result"]["axes"];

  std::vector<std::string> names;
  for (auto & value : resultAxes) {
    std::string n = value["name"].get<std::string>();
    names.push_back(n.c_str());
  }

  // NdmspcOutputFileOpen(cfg);

  if (_ndmspcVerbose >= 1) Printf("Starting User NdmspcProcess() ...");
  _currentBinCount++;
  bool ok = NdmspcProcessRecursiveInner(resultAxes.size() - 1, cfg, names);
  if (_ndmspcVerbose >= 1) Printf("User NdmspcProcess() done ...");

  // Store add _finalResults to final file
  NdmspcOutputFileClose();

  if (!_currentProcessOk) {
    if (_ndmspcVerbose >= 0) Printf("Skipping ...");
  }

  return true;
}

bool NdmspcProcessRecursive(Int_t i, json & cfg)
{
  if (i < 0) {
    return NdmspcProcessSinglePoint(cfg);
  }

  THnSparse * s = (THnSparse *)_currentInputObjects->At(0);
  TAxis *     a = (TAxis *)s->GetListOfAxes()->FindObject(cfg["ndmspc"]["cuts"][i]["axis"].get<std::string>().c_str());
  if (a == nullptr) return false;
  Int_t start = 1;
  Int_t end   = a->GetNbins();
  Int_t rebin = 1;
  if (cfg["ndmspc"]["cuts"][i]["rebin"].is_number_integer()) rebin = cfg["ndmspc"]["cuts"][i]["rebin"].get<Int_t>();

  if (rebin > 1) end /= rebin;

  if (cfg["ndmspc"]["process"]["ranges"].is_array()) {
    start = cfg["ndmspc"]["process"]["ranges"][i][0];
    if (cfg["ndmspc"]["process"]["ranges"][i][1] < end) end = cfg["ndmspc"]["process"]["ranges"][i][1];
  }

  for (Int_t iBin = start; iBin <= end; iBin++) {
    // Printf("ibin=%d", iBin);
    Int_t binMin                           = iBin;
    Int_t binMax                           = iBin;
    cfg["ndmspc"]["cuts"][i]["bin"]["min"] = binMin;
    cfg["ndmspc"]["cuts"][i]["bin"]["max"] = binMax;
    NdmspcProcessRecursive(i - 1, cfg);
  }
  return true;
}

bool NdmspcHandleInit(json & cfg, std::string extraPath = "")
{
  if (!cfg["ndmspc"]["process"]["type"].get<std::string>().compare("all") &&
      cfg["ndmspc"]["process"]["ranges"].is_null() &&
      !cfg["ndmspc"]["output"]["delete"].get<std::string>().compare("onInit")) {
    std::string outFileName;
    if (!cfg["ndmspc"]["output"]["host"].get<std::string>().empty() &&
        !cfg["ndmspc"]["output"]["dir"].get<std::string>().empty()) {
      // outFileName = cfg["ndmspc"]["output"]["host"].get<std::string>() + "/";

      outFileName += cfg["ndmspc"]["output"]["dir"].get<std::string>();
      outFileName += "/";
      for (auto & cut : cfg["ndmspc"]["cuts"]) {
        if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
        outFileName += cut["axis"].get<std::string>() + "_";
      }
      outFileName[outFileName.size() - 1] = '/';
      outFileName += "bins";

      if (!extraPath.empty()) {
        outFileName += "/" + extraPath;
        outFileName.pop_back();
      }
    }
    if (!outFileName.empty()) {
      Printf("Deleting output directory '%s' ...", outFileName.c_str());
      std::string rmUrl =
          TString::Format("%s/proc/user/?mgm.cmd=rm&mgm.path=%s&mgm.option=rf&mgm.format=json&filetype=raw",
                          cfg["ndmspc"]["output"]["host"].get<std::string>().c_str(), outFileName.c_str())
              .Data();

      if (_ndmspcVerbose >= 2) Printf("rmUrl '%s' ...", rmUrl.c_str());
      TFile * f = TFile::Open(rmUrl.c_str());
      if (!f) return 1;
      Printf("Directory '%s' deleted", outFileName.c_str());
      f->Close();
    }
    cfg["ndmspc"]["output"]["delete"] = "";
  }
  return true;
}

Int_t NdmspcProccessFile(json & cfg)
{

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
    json cuts;
    int  iCut = 0;
    for (auto & cut : cfg["ndmspc"]["cuts"]) {
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      cuts[iCut++] = cut;
    }
    cfg["ndmspc"]["cuts"] = cuts;

    NdmspcProcessRecursive(cfg["ndmspc"]["cuts"].size() - 1, cfg);
  }
  else {
    Printf("Error: Value [process][type]='%s' is not supported !!! Exiting ...", type.c_str());
    return 4;
  }
  NdmspcDestroy();
  return 0;
}
Int_t NdmspcPointRun(TString cfgFile = "", bool showConfig = false, TString ouputConfig = "")
{

  if (!NdmspcLoadConfig(cfgFile.Data(), cfg, showConfig, ouputConfig)) return 1;

  if (!cfg["ndmspc"]["data"]["histogram"].is_null() && !cfg["ndmspc"]["data"]["histogram"]["enabled"].is_null() &&
      cfg["ndmspc"]["data"]["histogram"]["enabled"].get<bool>() == true) {
    std::string fileNameHistogram = cfg["ndmspc"]["data"]["histogram"]["file"].get<std::string>();
    std::string objName           = cfg["ndmspc"]["data"]["histogram"]["obj"].get<std::string>();

    TFile * fProccessHistogram = TFile::Open(fileNameHistogram.c_str());
    if (!fProccessHistogram) {
      Printf("Error: Proccess input histogram file '%s' could not opened !!!", fileNameHistogram.c_str());
      return 1;
    }
    _currentProccessHistogram = (THnSparse *)fProccessHistogram->Get(objName.c_str());
    if (!_currentProccessHistogram) {
      Printf("Error: Proccess input histogram object '%s' could not opened !!!", objName.c_str());
      return 1;
    }
    TObjArray * axesArray = _currentProccessHistogram->GetListOfAxes();
    for (int iAxis = 0; iAxis < axesArray->GetEntries(); iAxis++) {

      TAxis * aTmp = (TAxis *)axesArray->At(iAxis);
      // Printf("axis=%s", aTmp->GetName());
      _currentProcessHistogramAxes.push_back(aTmp);
    }

    if (cfg["ndmspc"]["data"]["histogram"]["bins"].is_array()) {

      for (auto & v : cfg["ndmspc"]["data"]["histogram"]["bins"]) {
        Printf("%s", v.dump().c_str());
        int   i = 0;
        Int_t p[v.size()];
        for (auto & idx : v) {
          p[i] = idx;
          // _currentProcessHistogramPoint.push_back(idx);
          i++;
        }
        _currentProccessHistogram->SetBinContent(p, 1);
      }
    }
    _currentProccessHistogram->Print();
    if (_currentProccessHistogram->GetNbins()) {
      Int_t proccessPoint[_currentProccessHistogram->GetNdimensions()];
      for (int iBin = 0; iBin < _currentProccessHistogram->GetNbins(); iBin++) {
        _currentProcessHistogramPoint.clear();
        _currentProccessHistogram->GetBinContent(iBin, proccessPoint);
        // Printf("iBin=%d %d %d", iBin, proccessPoint[0], proccessPoint[1]);

        std::string path;
        for (auto & p : proccessPoint) {
          // printf("%d ", p);
          _currentProcessHistogramPoint.push_back(p);
          path += std::to_string(std::abs(p)) + "/";
        }
        // printf("\n");
        std::string fullPath = cfg["ndmspc"]["data"]["histogram"]["base"].get<std::string>();
        fullPath += "/";
        fullPath += path;
        fullPath += cfg["ndmspc"]["data"]["histogram"]["filename"].get<std::string>();
        // Printf("Path: %s %s", path.c_str(), fullPath.c_str());
        cfg["ndmspc"]["data"]["file"] = fullPath;

        cfg["ndmspc"]["output"]["post"] = path;

        // return 0;
        NdmspcHandleInit(cfg, path);
        Int_t rc = NdmspcProccessFile(cfg);
        if (rc) {
          return rc;
        }
      }
    }
    else {
      Printf("Error: No entries in proccess histogram !!! Nothing to process !!!");
      return 1;
    }
  }
  else {
    NdmspcHandleInit(cfg);
    return NdmspcProccessFile(cfg);
  }

  return 0;
}
