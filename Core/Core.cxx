#include <TSystem.h>
#include <cstdlib>
#include "TString.h"
#include "Core.h"
#include "Utils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Core);
/// \endcond

namespace Ndmspc {

/// Global configuration
json gCfg;

bool Core::LoadConfig(std::string config, std::string userConfig, std::string & environment, std::string userConfigRaw,
                      std::string binning)
{
  ///
  /// Load Config
  ///
  std::string fileContent = Utils::OpenRawFile(config);
  if (!fileContent.empty()) {
    gCfg = json::parse(fileContent);
    Printf("Using config file '%s' ...", config.c_str());
    if (!userConfig.empty()) {
      std::string fileContentUser = Ndmspc::Utils::OpenRawFile(userConfig);
      if (!fileContentUser.empty()) {
        json userCfg = json::parse(fileContentUser);
        gCfg.merge_patch(userCfg);
        Printf("User config file '%s' was merged ...", userConfig.c_str());
      }
      else {
        Printf("Warning: User config '%s' was specified, but it was not open !!!", userConfig.c_str());
        return false;
      }
    }
  }
  else {
    Printf("Error: Problem opening config file '%s' !!! Exiting ...", config.c_str());
    return false;
  }

  for (auto & cut : gCfg["ndmspc"]["cuts"]) {
    if (!cut["rebin"].is_number_integer()) gCfg["ndmspc"]["cuts"][cut["axis"].get<std::string>()] = 1;
    Int_t rebin       = 1;
    Int_t rebin_start = 1;
    if (cut["rebin"].is_number_integer()) rebin = cut["rebin"].get<Int_t>();
    if (cut["rebin_start"].is_number_integer()) rebin_start = cut["rebin_start"].get<Int_t>();
    if (rebin > 1 && rebin_start >= rebin) {
      Printf("Error: rebin_start=%d is greater than rebin=%d for axis '%s' !!! Please set rebin_start to lower then "
             "rebin !!! Exiting ...",
             rebin_start, rebin, cut["axis"].get<std::string>().c_str());
      gSystem->Exit(1);
    }
  }

  if (!environment.empty()) {
    gCfg["ndmspc"]["environment"] = environment;
    LoadEnvironment(environment);
  }
  else if (gCfg["ndmspc"]["environment"].is_string() && !gCfg["ndmspc"]["environment"].get<std::string>().empty()) {
    environment = gCfg["ndmspc"]["environment"].get<std::string>();
    LoadEnvironment(environment);
  }

  if (!userConfigRaw.empty()) {
    json userCfgRaw = json::parse(userConfigRaw);
    gCfg.merge_patch(userCfgRaw);
    Printf("Config raw '%s' was merged...", userConfigRaw.c_str());
  }
  Printf("Using binning '%s'", binning.c_str());
  if (!binning.empty()) {
    std::vector<std::string> ba    = Utils::Tokenize(binning.c_str(), '_');
    int                      i     = 0;
    int                      index = -1;
    for (auto & cut : gCfg["ndmspc"]["cuts"]) {
      index++;
      if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
      std::vector<std::string> b = Utils::Tokenize(ba[i].c_str(), '-');
      if (b.size() == 2) {
        gCfg["ndmspc"]["cuts"][index]["rebin"]       = atoi(b[0].c_str());
        gCfg["ndmspc"]["cuts"][index]["rebin_start"] = atoi(b[1].c_str());
      }
      i++;
    }
  }
  // exit(1);
  return true;
}
bool Core::LoadEnvironment(std::string environment)
{
  ///
  /// Load environment
  ///
  if (gCfg["ndmspc"]["environments"][environment].is_object()) {
    Printf("Using environment '%s' ...", environment.c_str());
    json myCfg = gCfg["ndmspc"]["environments"][environment];
    gCfg.merge_patch(myCfg);
  }
  else {
    if (!environment.compare("local")) {
      // Printf("Warning: No environment specified !!! Setting it to 'default' !!!");
      return true;
    }
    Printf("Error: Environment '%s' was not found !!! Exiting ...", environment.c_str());
    return false;
  }
  return true;
}
bool Core::SaveConfig(json cfg, std::string filename)
{
  ///
  /// Save config
  ///
  return Utils::SaveRawFile(filename, cfg.dump());
}
} // namespace Ndmspc
