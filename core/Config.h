#ifndef NdmspcConfig_H
#define NdmspcConfig_H
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <TNamed.h>
#include "InputMap.h"
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

  std::string              GetEnvironment() const;
  bool                     SetEnvironment(const std::string & environment = "");
  std::string              GetAnalysisBasePath() const;
  std::string              GetAnalysisInputPath() const;
  std::string              GetAnalysisName() const;
  std::string              GetAnalysisRevision() const;
  std::string              GetAnalysisNameRevision() const;
  std::string              GetInputPrefix() const;
  std::string              GetInputObjectDirecotry() const;
  std::vector<std::string> GetInputObjectNames() const;
  std::string              GetWorkspacePath() const;
  std::string              GetMapFileName() const;
  std::string              GetMapObjectName() const;

  // TODO:: Remove or modify
  bool        SetBinning(std::string binning = "");
  bool        SetBinningFromCutAxes(bool useMax = false);
  bool        SetInputMap();
  std::string GetBinning() const;
  std::string GetAxesName() const;
  std::string GetBaseDirectory() const;
  std::string GetContentFile() const;
  std::string GetWorkingDirectory() const;
  InputMap *  GetInputMap() const { return fInputMap; }
  std::string GetInputObjectDirectory() const;
  void GetInputObjectNames(std::vector<std::vector<std::string>> & names, std::vector<std::string> & aliases) const;

  // Delete copy constructor and assignment operator
  Config(const Config &)             = delete;
  Config & operator=(const Config &) = delete;

  // Static method to get the instance
  static Config * Instance(const std::string & config = "", const std::string & userConfig = "",
                           const std::string & userConfigRaw = "")
  {

    std::lock_guard<std::mutex> lock(fMutex);
    if (fgInstance == nullptr) {
      if (config.empty()) {
        return nullptr;
      }
      fgInstance = std::unique_ptr<Config>(new Config(config, userConfig, userConfigRaw));
    }

    return fgInstance.get();
  }

  private:
  static std::unique_ptr<Config> fgInstance;
  static std::mutex              fMutex;
  bool                           fCfgLoaded{false}; ///< configuration loaded
  json                           fCfg;              ///< configuration
  InputMap *                     fInputMap;         ///< input map
  /// \cond CLASSIMP
  ClassDef(Config, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
