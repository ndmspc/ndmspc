#include "Core.h"
#include "Config.h"
#include "InputMap.h"
#include "Results.h"
#include "Manager.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Manager);
/// \endcond

namespace Ndmspc {
Manager::Manager() : TNamed("ndmspcMgr", "NdmSpc Manager")
{
  ///
  /// Constructor
  ///
}
Manager::~Manager()
{
  ///
  /// Destructor
  ///
}
void Manager::Print(Option_t * option) const
{
  ///
  /// Print Manager information
  ///

  Printf("%s", std::string(64, '=').c_str());
  Printf("NdmSpc manager");
  Printf("Name: '%s' Title: '%s'", GetName(), GetTitle());

  Config & c = Config::Instance();
  c.Print(option);
  InputMap * im = c.GetInputMap();
  if (im) im->Print();
  Printf("%s", std::string(64, '=').c_str());
}
bool Manager::Load(std::string config, std::string userConfig, std::string environment, std::string userConfigRaw,
                   std::string binning)
{
  ///
  /// Load manager
  ///

  Config & c = Config::Instance();

  // INFO: Load configuration
  if (!c.Load(config, userConfig, userConfigRaw)) return false;

  // INFO: Load environment
  if (!c.SetEnvironment(environment)) return false;
  Printf("Environment '%s' is loaded ...", c.GetEnvironment().c_str());

  // INFO: Load environment
  if (!c.SetBinning(binning)) return false;

  // INFO: Load InputMap
  if (!c.SetInputMap()) return false;

  if (!fResults) fResults = new Results();
  fResults->Print();

  // if (!Core::Load(config, userConfig, environment, userConfigRaw, binning)) return false;

  // std::string hostUrl = gCfg["ndmspc"]["output"]["host"].get<std::string>();
  // std::string path;
  // if (!hostUrl.empty()) path = hostUrl + "/";
  // path += gCfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";

  // for (auto & cut : gCfg["ndmspc"]["cuts"]) {
  //   if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
  //   path += cut["axis"].get<std::string>() + "_";
  //   fCuts.push_back(cut["axis"].get<std::string>());
  // }
  //
  // path[path.size() - 1] = '/';
  //
  // path += environment + "/";
  //
  // fInputFileName = path + fResultFileName;

  return true;
}
} // namespace Ndmspc
