#include <TSystem.h>
#include "Utils.h"
#include "Logger.h"

#include "Config.h"
/// \cond CLASSIMP
ClassImp(Ndmspc::Config);
/// \endcond

namespace Ndmspc {
Config::Config() : TNamed("ndmspcCfg", "NDMSPC configuration") {}
Config::Config(const std::string & config, const std::string & userConfig, const std::string & userConfigRaw)
    : TNamed("ndmspcCfg", "NDMSPC configuration")
{
  ///
  /// Constructor
  ///

  // INFO: Load configuration
  Load(config, userConfig, userConfigRaw);
}
Config::~Config() {}
bool Config::Load(const std::string & config, const std::string & userConfig, const std::string & userConfigRaw)
{
  ///
  /// Load configuration
  ///

  auto logger             = Ndmspc::Logger::getInstance("");
  fCfgLoaded              = false;
  std::string fileContent = Utils::OpenRawFile(config);
  if (!fileContent.empty()) {
    fCfg = json::parse(fileContent);
    logger->Info("Using config file '%s' ...", config.c_str());
    if (!userConfig.empty()) {
      std::string fileContentUser = Ndmspc::Utils::OpenRawFile(userConfig);
      if (!fileContentUser.empty()) {
        json userCfg = json::parse(fileContentUser);
        fCfg.merge_patch(userCfg);
        logger->Info("User config file '%s' was merged ...", userConfig.c_str());
      }
      else {
        logger->Warning("User config '%s' was specified, but it was not open !!!", userConfig.c_str());
        return false;
      }
    }
    if (!userConfigRaw.empty()) {
      json userCfgRaw = json::parse(userConfigRaw);
      fCfg.merge_patch(userCfgRaw);
      logger->Info("Config raw '%s' was merged...", userConfigRaw.c_str());
    }
  }
  else {
    logger->Error("Problem opening config file '%s' !!! Exiting ...", config.c_str());
    return false;
  }

  // INFO: Set default binning from configuration
  if (!SetBinning()) return false;

  fCfgLoaded = true;

  return fCfgLoaded;
}
void Config::Print(Option_t * option) const
{
  ///
  /// Print configuration
  ///

  std::string opt = option;

  Printf("Configuration:");
  Printf("  - Environment: '%s'", GetEnvironment().c_str());
  Printf("  - Binning: '%s'", GetBinning().c_str());
  Printf("  - Cuts:");
  for (auto & cut : fCfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    Printf("    - %s: rebin=%d rebin_start=%d", cut["axis"].get<std::string>().c_str(), cut["rebin"].get<Int_t>(),
           cut["rebin_start"].get<Int_t>());
  }
  Printf("  - Work Dir: '%s'", GetWorkingDirectory().c_str());
  Printf("  - Content file: '%s'", GetContentFile().c_str());

  if (opt.find("config") != std::string::npos) {
    Printf("Config: \n%s", fCfg.dump(2).c_str());
  }
}

std::string Config::GetAxesName() const
{
  ///
  /// Return axes name
  ///

  std::string axes = "";
  for (auto & cut : fCfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    axes += cut["axis"].get<std::string>() + "_";
  }
  axes.pop_back();
  return axes;
}

std::string Config::GetBaseDirectory() const
{
  ///
  /// Return base directory
  ///

  std::string path = "";
  if (fCfg["ndmspc"]["output"]["host"].is_string()) {
    path = fCfg["ndmspc"]["output"]["host"].get<std::string>();
  }
  if (fCfg["ndmspc"]["output"]["dir"].is_string()) {
    path += fCfg["ndmspc"]["output"]["dir"].get<std::string>();
  }
  return gSystem->ExpandPathName(path.c_str());
}

std::string Config::GetWorkingDirectory() const
{
  ///
  /// Return working directory
  ///

  std::string wd = GetBaseDirectory();
  wd += "/" + GetEnvironment() + "/" + GetAxesName() + "/" + GetBinning();
  return wd;
}

std::string Config::GetContentFile() const
{
  ///
  /// Return content file
  ///

  std::string file = "";
  if (fCfg["ndmspc"]["output"]["file"].is_string()) {
    file = fCfg["ndmspc"]["output"]["file"].get<std::string>();
  }
  return file;
}

std::string Config::GetEnvironment() const
{
  ///
  /// Return environment
  ///

  if (fCfg["ndmspc"]["environment"].is_string() && !fCfg["ndmspc"]["environment"].get<std::string>().empty()) {
    return fCfg["ndmspc"]["environment"].get<std::string>();
  }
  return "";
}
bool Config::SetEnvironment(const std::string & environment)
{
  ///
  /// Set environment
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  if (fCfg["ndmspc"]["environments"][environment].is_object()) {
    // Printf("Setting up environment '%s' ...", environment.c_str());
    json myCfg = fCfg["ndmspc"]["environments"][environment];
    fCfg.merge_patch(myCfg);
  }
  else {
    if (environment.empty()) {
      Printf("Warning: No environment specified !!! Trying to set it to 'local' !!!");
      if (fCfg["ndmspc"]["environments"]["local"].is_object()) {
        json myCfg                    = fCfg["ndmspc"]["environments"]["local"];
        fCfg["ndmspc"]["environment"] = "local";
        fCfg.merge_patch(myCfg);
        return true;
      }
      logger->Error("Error: Environment 'local' was not found !!! Exiting ...");
      return false;
    }
    logger->Error("Error: Environment '%s' was not found !!! Exiting ...", environment.c_str());
    return false;
  }

  return true;
}
std::string Config::GetBinning() const
{
  ///
  /// Return binning
  ///

  std::string binning = "";

  if (!fCfg["ndmspc"].contains("binning")) return binning;
  if (!fCfg["ndmspc"]["binning"].is_string()) return binning;
  binning = fCfg["ndmspc"]["binning"].get<std::string>();

  return binning;
}
bool Config::SetBinning(std::string binning)
{
  ///
  /// Set binning
  ///

  if (binning.empty()) {
    binning = GetBinning();
    if (binning.empty()) {
      if (!SetBinningFromCutAxes(false)) {
        binning = GetBinning();
        if (binning.empty()) {
          if (!SetBinningFromCutAxes(true)) return false;
        }
      }
    }
    return true;
  }

  std::vector<std::string> ba    = Utils::Tokenize(binning.c_str(), '_');
  int                      i     = 0;
  int                      index = -1;
  for (auto & cut : fCfg["ndmspc"]["cuts"]) {
    index++;
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    std::vector<std::string> b = Utils::Tokenize(ba[i].c_str(), '-');
    if (b.size() == 2) {
      fCfg["ndmspc"]["cuts"][index]["rebin"]       = atoi(b[0].c_str());
      fCfg["ndmspc"]["cuts"][index]["rebin_start"] = atoi(b[1].c_str());
    }
    i++;
  }
  fCfg["ndmspc"]["binning"] = binning;
  return true;
}
bool Config::SetBinningFromCutAxes(bool useMax)
{
  ///
  /// Guess binning from cut axes
  ///

  if (!fCfg["ndmspc"]["cuts"].is_array()) return false;

  std::string binning = GetBinning();
  int         index   = -1;
  for (auto & cut : fCfg["ndmspc"]["cuts"]) {
    index++;
    if (!cut["enabled"].is_boolean() || cut["enabled"].get<bool>() == false) continue;
    if (useMax) {
      binning += Form("%d-%d_", cut["nbins"].get<Int_t>(), 1);
      fCfg["ndmspc"]["cuts"][index]["rebin"]       = cut["nbins"].get<Int_t>();
      fCfg["ndmspc"]["cuts"][index]["rebin_start"] = 1;
    }
    else {
      binning += Form("%d-%d_", cut["rebin"].get<Int_t>(), cut["rebin_start"].get<Int_t>());
    }
  }
  binning.pop_back();
  fCfg["ndmspc"]["binning"] = binning;
  return true;
}
bool Config::SetInputMap()
{
  ///
  /// Set input map
  ///
  auto logger = Ndmspc::Logger::getInstance("");

  if (!fCfg["ndmspc"]["data"]["map"]["enabled"].is_boolean()) return false;
  if (!fCfg["ndmspc"]["data"]["map"]["enabled"].get<bool>()) return false;

  fInputMap = new InputMap(fCfg["ndmspc"]["data"]["map"]);
  if (!fInputMap->LoadMap()) {
    logger->Error("Cannot load input map!!!");
    return false;
  }
  fInputMap->Query(GetBaseDirectory());
  return true;
}

std::string Config::GetInputObjectDirectory() const
{
  ///
  /// Return input object directory
  ///

  std::string path = "";
  if (fCfg["ndmspc"]["data"]["directory"].is_string()) {
    path = fCfg["ndmspc"]["data"]["directory"].get<std::string>() + '/';
  }
  return path;
}
void Config::GetInputObjectNames(std::vector<std::vector<std::string>> & names,
                                 std::vector<std::string> &              aliases) const
{
  ///
  /// Get input object names
  ///

  if (!fCfg["ndmspc"]["data"]["objects"].is_array()) return;

  for (auto & obj : fCfg["ndmspc"]["data"]["objects"]) {

    std::vector<std::string> obj_alias = Utils::Tokenize(obj.get<std::string>(), ':');
    std::vector<std::string> obj_name  = Utils::Tokenize(obj_alias[0], '+');

    names.push_back(obj_name);
    aliases.push_back(obj_alias[1]);
  }
}

} // namespace Ndmspc
