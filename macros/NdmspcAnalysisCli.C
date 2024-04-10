#include <TFile.h>
#include <TH1.h>
#include <TString.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "NdmspcMacro.C"

using json = nlohmann::json;

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
    projectDir = "root://eos.ndmspc.io//eos/ndmspc/scratch/temp/" + user + "/" + name;
  }
  TUrl        projectUrl(projectDir.c_str());
  std::string host = projectUrl.GetHost();

  


  json        cfgOutput;
  cfgOutput["analysis"]                       = name.c_str();
  cfgOutput["analyses"][name]["name"]         = name.c_str();
  cfgOutput["analyses"][name]["description"]  = description.c_str();
  cfgOutput["analyses"][name]["base"]["http"] = "https://" + host;
  cfgOutput["analyses"][name]["base"]["root"] = "root://" + host + "/";
  cfgOutput["analyses"][name]["base"]["path"] = projectUrl.GetFile();

  std::string xrootPath               = "root://" + host + "/" + projectUrl.GetFile() + "/" + "hMap.root";
  cfgOutput["analyses"][name]["file"] = "https://" + host + projectUrl.GetFile() + "/" + "hMap.root";
  cfgOutput["analyses"][name]["obj"]  = "hMap";



std::string axis = "axis1-pt";

// if (cfg["ndmspc"]["cuts"].size()>0) {
//   for (auto & element : cfg["ndmspc"]["cuts"]) {
//   //   cfgOutput["analyses"][name]["projection"]                       = axis;
//   // cfgOutput["analyses"][name]["projections"][axis]["name"]        = axis;
//   // cfgOutput["analyses"][name]["projections"][axis]["description"] = axis;
//   // cfgOutput["analyses"][name]["projections"][axis]["file"] =
//   //     "https://" + host + "/" + projectUrl.GetFile() + "/" + axis + "/hProjMap.root";
//   // cfgOutput["analyses"][name]["projections"][axis]["obj"]     = "hProjMap";
//   // cfgOutput["analyses"][name]["projections"][axis]["axes"][0] = axis;
  
//   }
// }

  cfgOutput["analyses"][name]["projection"]                       = axis;
  cfgOutput["analyses"][name]["projections"][axis]["name"]        = axis;
  cfgOutput["analyses"][name]["projections"][axis]["description"] = axis;
  cfgOutput["analyses"][name]["projections"][axis]["file"] =
      "https://" + host + "/" + projectUrl.GetFile() + "/" + axis + "/hProjMap.root";
  cfgOutput["analyses"][name]["projections"][axis]["obj"]     = "hProjMap";
  cfgOutput["analyses"][name]["projections"][axis]["axes"][0] = axis;
  


  cfgOutput["analyses"][name]["plugin"]                                = "default";
  cfgOutput["analyses"][name]["plugins"]["NdmspcBinPlugin"]["current"] = "default";


  std::string sw = projectDir + "/sw/NdmspcMacro.C|NdmspcMacro.C";

  if (cfg["ndmspc"]["job"]["inputs"].is_null() || cfg["ndmspc"]["job"]["inputs"].is_array() ||
      cfg["ndmspc"]["job"]["inputs"].size() == 0) {
    cfg["ndmspc"]["job"]["inputs"] = {sw.c_str()};
  }

  std::string inputFileDefaltConfig = cfg["ndmspc"]["data"]["file"].get<std::string>();
  json objectsDefaltConfig   = cfg["ndmspc"]["data"]["objects"];

  std::string   cfgFileName = name + ".json";
  std::ifstream f(cfgFileName);
  if (f.is_open()) {
    json cfgJson = json::parse(f);
    cfg.merge_patch(cfgJson);
    cfg["ndmspc"]["data"]["objects"] = objectsDefaltConfig;
  }
  else {
    cfg["ndmspc"]["data"]["file"] = projectDir + "/inputs/" + inputFileDefaltConfig;
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

  // Printf("%s", cfg.dump(2).c_str());
  std::ofstream fileCfg(cfgFileName.c_str());
  fileCfg << std::setw(2) << cfg << std::endl;

  cfgOutput["analyses"][name]["plugins"]["NdmspcBinPlugin"]["default"]["config"] = cfg;

  cfgOutput["plugin"] = "NdmspcBinPlugin";

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
    TMacro      m(TString::Format("%s/NdmspcCreateMap.C", gSystem->Getenv("NDMSPC_MACRO_DIR")).Data());
    std::string mArgs = "\"" + cfg["ndmspc"]["data"]["file"].get<std::string>() + "\",\"" +
                        cfg["ndmspc"]["data"]["objects"][0].get<std::string>() + "\",\"\",\"" + xrootPath + "\",\"" +
                        cfgOutput["analyses"][name]["obj"].get<std::string>() + "\"";
    // Printf("%s", mArgs.c_str());
    m.Exec(mArgs.c_str());
  }
  std::ofstream file(output.c_str());
  file << std::setw(2) << cfgOutput << std::endl;
  Printf("Config file '%s' was generated for workspace located at '%s' ...", output.c_str(), projectDir.c_str());
  return 0;
}
