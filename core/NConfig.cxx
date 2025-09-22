#include <TSystem.h>
#include <vector>
#include "NUtils.h"
#include "NLogger.h"

#include "NConfig.h"
/// \cond CLASSIMP
ClassImp(Ndmspc::NConfig);
/// \endcond

namespace Ndmspc {

// Initialize pointer to null
std::unique_ptr<NConfig> NConfig::fgInstance = nullptr;
std::mutex               NConfig::fMutex;

NConfig::NConfig() : TNamed("ndmspcCfg", "NDMSPC configuration") {}
NConfig::NConfig(const std::string & config, const std::string & userNConfig, const std::string & userNConfigRaw)
    : TNamed("ndmspcCfg", "NDMSPC configuration")
{
  ///
  /// Constructor
  ///

  // INFO: Load configuration
  Load(config, userNConfig, userNConfigRaw);
}
NConfig::~NConfig() {}
bool NConfig::Load(const std::string & config, const std::string & userNConfig, const std::string & userNConfigRaw)
{
  ///
  /// Load configuration
  ///

  NLogger::Debug("Loading configuration from '%s' ['%s','%s']...", config.c_str(), userNConfig.c_str(),
                 userNConfigRaw.c_str());

  fCfgLoaded = false;
  fCfg       = json::object();
  if (!config.empty()) {
    std::string fileContent = NUtils::OpenRawFile(config);
    if (!fileContent.empty()) {
      fCfg = json::parse(fileContent);
      NLogger::Info("Using config file '%s' ...", config.c_str());
    }
    else {
      NLogger::Error("Problem opening config file '%s' !!! Exiting ...", config.c_str());
      return false;
    }
  }
  if (!userNConfig.empty()) {
    std::string fileContentUser = Ndmspc::NUtils::OpenRawFile(userNConfig);
    if (!fileContentUser.empty()) {
      json userCfg = json::parse(fileContentUser);
      fCfg.merge_patch(userCfg);
      NLogger::Info("User config file '%s' was merged ...", userNConfig.c_str());
    }
    else {
      NLogger::Warning("User config '%s' was specified, but it was not open !!!", userNConfig.c_str());
      return false;
    }
  }
  if (!userNConfigRaw.empty()) {
    json userCfgRaw = json::parse(userNConfigRaw);
    fCfg.merge_patch(userCfgRaw);
    NLogger::Info("NConfig raw '%s' was merged...", userNConfigRaw.c_str());
  }

  // INFO: Set default binning from configuration
  // if (!SetBinning()) return false;

  fCfgLoaded = true;

  return fCfgLoaded;
}
void NConfig::Print(Option_t * option) const
{
  ///
  /// Print configuration
  ///

  if (!fCfgLoaded) {
    NLogger::Error("Error: Configuration was not loaded !!!");
    return;
  }

  std::string opt = option;

  NLogger::Info("NDMSPC configuration:");
  NLogger::Info("  - Name: '%s'", GetAnalysisName().c_str());
  NLogger::Info("  - Revision: '%s'", GetAnalysisRevision().c_str());
  NLogger::Info("  - Environment: '%s'", GetEnvironment().c_str());
  NLogger::Info("  - Base path: '%s'", GetAnalysisBasePath().c_str());
  NLogger::Info("  - Workspace path: '%s'", GetWorkspacePath().c_str());
  NLogger::Info("  - Map file: '%s'", GetMapFileName().c_str());
  NLogger::Info("  - Map object: '%s'", GetMapObjectName().c_str());
  NLogger::Info("  - Input prefix: '%s'", GetInputPrefix().c_str());
  NLogger::Info("  - Input object directory: '%s'", GetInputObjectDirecotry().c_str());

  // Print input objects
  NLogger::Info("  - Input objects:");
  for (auto & obj : GetInputObjectNames()) {
    NLogger::Info("    - '%s'", obj.c_str());
  }

  // Printf("  - Binning: '%s'", GetBinning().c_str());
  // Printf("  - Cuts:");
  // for (auto & cut : fCfg["ndmspc"]["cuts"]) {
  //   if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
  //   Printf("    - %s: rebin=%d rebin_start=%d", cut["axis"].get<std::string>().c_str(), cut["rebin"].get<Int_t>(),
  //          cut["rebin_start"].get<Int_t>());
  // }
  // Printf("  - Work Dir: '%s'", GetWorkingDirectory().c_str());
  // Printf("  - Content file: '%s'", GetContentFile().c_str());
  //
  if (opt.find("config") != std::string::npos) {
    Printf("NConfig: \n%s", fCfg.dump(2).c_str());
  }
}

std::string NConfig::GetAnalysisBasePath() const
{
  ///
  /// Return analysis path
  ///

  return gSystem->ExpandPathName(NUtils::GetJsonString(fCfg["ndmspc"]["base"]).c_str());
}
std::string NConfig::GetWorkspacePath() const
{
  ///
  /// Return workspace path
  ///

  std::string dir  = NUtils::GetJsonString(fCfg["ndmspc"]["workspace"]["dir"]);
  std::string host = NUtils::GetJsonString(fCfg["ndmspc"]["workspace"]["host"]);
  std::string path = host + dir;
  return gSystem->ExpandPathName(path.c_str());
}

std::string NConfig::GetAnalysisInputPath() const
{
  ///
  /// Return analysis input path
  ///

  std::string path = GetAnalysisBasePath() + "/" + GetAnalysisName() + "/" + GetInputPrefix();
  return gSystem->ExpandPathName(path.c_str());
}

std::string NConfig::GetAnalysisName() const
{
  ///
  /// Return analysis name
  ///

  return NUtils::GetJsonString(fCfg["ndmspc"]["name"]);
}

std::string NConfig::GetAnalysisRevision() const
{
  ///
  /// Return analysis revision
  ///

  return NUtils::GetJsonString(fCfg["ndmspc"]["revision"]);
}

std::string NConfig::GetAnalysisNameRevision() const
{
  ///
  /// Return analysis name and revision
  ///

  std::string name = GetAnalysisName();
  std::string rev  = GetAnalysisRevision();
  if (rev.empty()) return name;
  return name + "_" + rev;
}
std::string NConfig::GetAnalysisPath() const
{
  ///
  /// Return analysis path
  ///
  std::string path = GetAnalysisBasePath() + "/" + GetAnalysisNameRevision();
  return path;
}

std::string NConfig::GetInputPrefix() const
{
  ///
  /// Return input prefix
  ///

  return NUtils::GetJsonString(fCfg["ndmspc"]["input"]["prefix"]);
}

std::string NConfig::GetInputObjectDirecotry() const
{
  ///
  /// Return input object directory
  ///

  return NUtils::GetJsonString(fCfg["ndmspc"]["input"]["directory"]);
}

std::vector<std::string> NConfig::GetInputObjectNames() const
{
  ///
  /// Return input object names
  ///

  return NUtils::GetJsonStringArray(fCfg["ndmspc"]["input"]["objects"]);
}
std::string NConfig::GetMapFileName() const
{
  ///
  /// Return map file name
  ///

  return NUtils::GetJsonString(fCfg["ndmspc"]["map"]["file"]);
}
std::string NConfig::GetMapObjectName() const
{
  ///
  /// Return map object name
  ///

  return NUtils::GetJsonString(fCfg["ndmspc"]["map"]["object"]);
}

std::string NConfig::GetEnvironment() const
{
  ///
  /// Return environment
  ///

  if (fCfg["ndmspc"]["environment"].is_string() && !fCfg["ndmspc"]["environment"].get<std::string>().empty()) {
    return fCfg["ndmspc"]["environment"].get<std::string>();
  }
  return "";
}
bool NConfig::SetEnvironment(const std::string & environment)
{
  ///
  /// Set environment
  ///
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
      NLogger::Error("Error: Environment 'local' was not found !!! Exiting ...");
      return false;
    }
    NLogger::Error("Error: Environment '%s' was not found !!! Exiting ...", environment.c_str());
    return false;
  }

  return true;
}

} // namespace Ndmspc
