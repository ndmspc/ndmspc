#include "Core.h"
#include "Utils.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::Core);
/// \endcond

namespace NdmSpc {

/// Global configuration
json gCfg;

bool Core::LoadConfig(std::string config, std::string userConfig, std::string environment)
{
  std::string fileContent = Utils::OpenRawFile(config);
  if (!fileContent.empty()) {
    gCfg = json::parse(fileContent);
    Printf("Using config file '%s' ...", config.c_str());
    if (!userConfig.empty()) {
      std::string fileContentUser = NdmSpc::Utils::OpenRawFile(userConfig);
      if (!fileContentUser.empty()) {
        json userCfg = json::parse(fileContentUser);
        gCfg.merge_patch(userCfg);
        Printf("Merging user config file '%s' ...", userConfig.c_str());
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

  if (!environment.empty()) {
    gCfg["ndmspc"]["environment"] = environment;
    LoadEnvironment(environment);
  }
  else if (gCfg["ndmspc"]["environment"].is_string() && !gCfg["ndmspc"]["environment"].get<std::string>().empty()) {
    environment = gCfg["ndmspc"]["environment"].get<std::string>();
    LoadEnvironment(environment);
  }
  return true;
}
bool Core::LoadEnvironment(std::string environment)
{
  if (gCfg["ndmspc"]["environments"][environment].is_object()) {
    Printf("Using environment '%s' ...", environment.c_str());
    json myCfg = gCfg["ndmspc"]["environments"][environment];
    gCfg.merge_patch(myCfg);
  }
  else {
    if (!environment.compare("default")) {
      // Printf("Warning: No environment specified !!! Setting it to 'default' !!!");
      return true;
    }
    Printf("Error: Environment '%s' was not found !!! Exiting ...", environment.c_str());
    return false;
  }
  return true;
}
} // namespace NdmSpc
