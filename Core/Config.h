#ifndef NdmspcConfig_H
#define NdmspcConfig_H
#include <TNamed.h>
#include "InputMap.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace Ndmspc {

///
/// \class Config
///
/// \brief Config object
///	\author Martin Vala <mvala@cern.ch>
///

class Config : public TNamed {
  public:
  Config();
  Config(const std::string & config, const std::string & userConfig = "", const std::string & userConfigRaw = "");
  virtual ~Config();
  virtual void Print(Option_t * option = "") const;

  json & GetCfg() { return fCfg; }
  bool   Load(const std::string & config, const std::string & userConfig = "", const std::string & userConfigRaw = "");
  bool   IsLoaded() { return fCfgLoaded; }
  bool   SetEnvironment(const std::string & environment = "");
  bool   SetBinning(std::string binning = "");
  bool   SetBinningFromCutAxes(bool useMax = false);
  bool   SetInputMap();

  std::string GetEnvironment() const;
  std::string GetBinning() const;
  std::string GetAxesName() const;
  std::string GetBaseDirectory() const;
  std::string GetContentFile() const;
  std::string GetWorkingDirectory() const;
  InputMap *  GetInputMap() const { return fInputMap; }

  // Delete copy constructor and assignment operator
  Config(const Config &)             = delete;
  Config & operator=(const Config &) = delete;

  // Static method to get the instance
  static Config & Instance()
  {
    static Config instance; // Guaranteed to be created only once (C++11 thread-safe)
    return instance;
  }

  private:
  bool       fCfgLoaded{false}; ///< configuration loaded
  json       fCfg;              ///< configuration
  InputMap * fInputMap;         ///< input map
  /// \cond CLASSIMP
  ClassDef(Config, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
