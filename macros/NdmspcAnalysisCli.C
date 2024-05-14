#include <TFile.h>
#include <TH1.h>
#include <TString.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "NdmspcPointMacro.C"

using json = nlohmann::json;

std::vector<std::string> tokenize(std::string_view input, const char delim)
{
  std::vector<std::string> out;

  for (auto found = input.find(delim); found != std::string_view::npos; found = input.find(delim)) {
    out.emplace_back(input, 0, found);
    input.remove_prefix(found + 1);
  }

  if (not input.empty()) out.emplace_back(input);

  return out;
}

int NdmspcAnalysisCli(std::string name = "myAnalysis", std::string description = "", std::string projectDir = "",
                      bool forceCopy = false)
{

  json cfg;
  NdmspcDefaultConfig(cfg);

  std::string output = name + ".ndmspc";
  if (projectDir.empty()) {
    std::string user = gSystem->GetUserInfo()->fUser.Data();
    projectDir       = "root://eos.ndmspc.io//eos/ndmspc/scratch/temp/" + user + "/" + name;
  }
  TUrl        projectUrl(projectDir.c_str());
  std::string host      = projectUrl.GetHost();
  std::string xrootPath = "root://" + host + "/" + projectUrl.GetFile() + "/" + "hMap.root";

  json cfgOutput;

  std::ifstream fOutput(output);
  if (fOutput.is_open()) {
    cfgOutput = json::parse(fOutput);
  }
  else {

    cfgOutput["analysis"]                       = name.c_str();
    cfgOutput["analyses"][name]["name"]         = name.c_str();
    cfgOutput["analyses"][name]["description"]  = description.c_str();
    cfgOutput["analyses"][name]["base"]["http"] = "https://" + host;
    cfgOutput["analyses"][name]["base"]["root"] = "root://" + host + "/";
    cfgOutput["analyses"][name]["base"]["path"] = projectUrl.GetFile();

    cfgOutput["analyses"][name]["file"] = "https://" + host + projectUrl.GetFile() + "/" + "hMap.root";
    cfgOutput["analyses"][name]["obj"]  = "hMap";

    cfgOutput["analyses"][name]["plugin"]                                = "default";
    cfgOutput["analyses"][name]["plugins"]["NdmspcBinPlugin"]["current"] = "default";

    std::string sw = projectDir + "/sw/NdmspcPointMacro.C|NdmspcPointMacro.C";

    if (cfg["ndmspc"]["job"]["inputs"].is_null() || cfg["ndmspc"]["job"]["inputs"].is_array() ||
        cfg["ndmspc"]["job"]["inputs"].size() == 0) {
      cfg["ndmspc"]["job"]["inputs"] = {sw.c_str()};
    }
  }

  std::string inputFileDefaltConfig = cfg["ndmspc"]["data"]["file"].get<std::string>();
  json        objectsDefaltConfig   = cfg["ndmspc"]["data"]["objects"];

  std::string   cfgFileName = name + ".json";
  std::ifstream f(cfgFileName);
  if (f.is_open()) {
    json cfgJson = json::parse(f);
    // cfgJson["ndmspc"]["cuts"] = cfg["ndmspc"]["cuts"];
    cfg.merge_patch(cfgJson);
    cfg["ndmspc"]["data"]["objects"] = objectsDefaltConfig;
  }
  else {
    if (inputFileDefaltConfig.rfind("root://", 0) != 0) {
      cfg["ndmspc"]["data"]["file"] = projectDir + "/inputs/" + inputFileDefaltConfig;
    }
  }

  std::string       srcName, srcSingleName;
  std::stringstream sSrcfull(objectsDefaltConfig[0].get<std::string>().c_str());
  getline(sSrcfull, srcName, ':');
  std::stringstream sSrcName(srcName.c_str());
  getline(sSrcName, srcSingleName, '+');

  if (cfg["ndmspc"]["cuts"].size() == 0) {
    // Generate all axis
    // {"enabled": false, "axis": "hNSparseAxisName", "bin" : {"min":3, "max": 3}, "rebin":1}
    TFile * tmpFile = TFile::Open(inputFileDefaltConfig.c_str());
    if (!tmpFile) {
      Printf("Error: Problem opening file '%s' !!! Exiting ...", inputFileDefaltConfig.c_str());
      return 1;
    }

    THnSparse * tmpSparse = (THnSparse *)tmpFile->Get(srcSingleName.c_str());
    if (!tmpSparse) {
      Printf("Error: Problem opening object '%s' !!! Exiting ...", objectsDefaltConfig[0].get<std::string>().c_str());
      return 1;
    }
    tmpSparse->ls();
    TObjArray * axes = tmpSparse->GetListOfAxes();
    TAxis *     a;
    for (int i = 0; i < axes->GetEntries(); i++) {
      a = (TAxis *)axes->At(i);
      if (!a) continue;
      // a->Print();
      Printf("Init axis '%s' with enabled=false", a->GetName());
      cfg["ndmspc"]["cuts"][i]["enabled"]    = false;
      cfg["ndmspc"]["cuts"][i]["axis"]       = a->GetName();
      cfg["ndmspc"]["cuts"][i]["bin"]["min"] = 1;
      cfg["ndmspc"]["cuts"][i]["bin"]["max"] = 1;
      cfg["ndmspc"]["cuts"][i]["rebin"]      = 1;
    }

    tmpFile->Close();
  }

  std::string axisName;
  if (cfg["ndmspc"]["cuts"].size() > 0) {

    for (auto & cut : cfg["ndmspc"]["cuts"]) {
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      if (axisName.length() > 0) axisName += "_";
      axisName += cut["axis"].get<std::string>();
    }

    if (!axisName.empty()) {

      int i                                                               = 0;
      cfgOutput["analyses"][name]["projection"]                           = axisName;
      cfgOutput["analyses"][name]["projections"][axisName]["name"]        = axisName;
      cfgOutput["analyses"][name]["projections"][axisName]["description"] = axisName;
      cfgOutput["analyses"][name]["projections"][axisName]["file"] =
          "https://" + host + "/" + projectUrl.GetFile() + "/" + axisName + "/hProjMap.root";
      cfgOutput["analyses"][name]["projections"][axisName]["obj"]  = "hProjMap";
      cfgOutput["analyses"][name]["projections"][axisName]["axes"] = {};
      for (auto & cut : cfg["ndmspc"]["cuts"]) {
        if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
        cfgOutput["analyses"][name]["projections"][axisName]["axes"][i++] = cut["axis"].get<std::string>();
      }
    }
  }

  std::string inputFileRemote = cfg["ndmspc"]["data"]["file"].get<std::string>();
  bool        inputFileCopied = false;

  if (forceCopy) {
    Printf("Copy '%s' to '%s' ...", inputFileDefaltConfig.c_str(), inputFileRemote.c_str());
    TFile::Cp(inputFileDefaltConfig.c_str(), inputFileRemote.c_str());
    inputFileCopied = true;
  }
  else {
    TFile * fInput = TFile::Open(inputFileRemote.c_str());
    if (fInput) {
      Printf("Note: Skipping to copy file '%s' (already copied).", inputFileRemote.c_str());
    }
    else {
      // cfg["ndmspc"]["data"]["file"] = projectDir + "/inputs/" + inputFileRemote;
      Printf("Copy '%s' to '%s' ...", inputFileDefaltConfig.c_str(), inputFileRemote.c_str());
      TFile::Cp(inputFileDefaltConfig.c_str(), inputFileRemote.c_str());
      inputFileCopied = true;
    }
  }

  cfgOutput["analyses"][name]["plugins"]["NdmspcBinPlugin"]["default"]["config"] = cfg;
  cfgOutput["plugin"]                                                            = "NdmspcBinPlugin";

  if (!axisName.empty()) {
    Printf("Current cut axis name: %s", axisName.c_str());
    std::string binsDir = projectUrl.GetFile();
    // binsDir += "/" + axisName + "/bins";
    cfg["ndmspc"]["output"]["dir"]  = binsDir;
    cfg["ndmspc"]["output"]["host"] = "root://" + host + "/";
  }
  // Printf("%s", cfg.dump(2).c_str());
  std::ofstream fileCfg(cfgFileName.c_str());
  fileCfg << std::setw(2) << cfg << std::endl;

  for (auto & element : cfg["ndmspc"]["job"]["inputs"]) {
    std::vector<std::string> e = tokenize(element.get<std::string>(), '|');
    // Printf("%s %s", e[1].c_str(), e[0].c_str());
    if (e[0].rfind("root://", 0) == 0) {
      Printf("Copy '%s' to '%s' ...", e[1].c_str(), e[0].c_str());
      TFile::Cp(e[1].c_str(), e[0].c_str());
    }
  }

  // cfg["ndmspc"]["log"]["type"] = "error-only";

  if (inputFileCopied) {
    Printf("Creating map file ... ");
    TMacro      m(TString::Format("%s/NdmspcCreateMap.C", gSystem->Getenv("NDMSPC_MACRO_DIR")).Data());
    std::string mArgs = "\"" + cfg["ndmspc"]["data"]["file"].get<std::string>() + "\",\"" + srcSingleName +
                        "\",\"\",\"" + xrootPath + "\",\"" + cfgOutput["analyses"][name]["obj"].get<std::string>() +
                        "\"";
    // Printf("%s", mArgs.c_str());
    m.Exec(mArgs.c_str());
  }
  std::ofstream file(output.c_str());
  file << std::setw(2) << cfgOutput << std::endl;
  Printf("Config file '%s' was generated for workspace located at '%s' ...", output.c_str(), projectDir.c_str());
  return 0;
}
