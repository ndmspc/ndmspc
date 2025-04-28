#ifndef NdmspcNConfig_H
#define NdmspcNConfig_H
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include <TNamed.h>
using json = nlohmann::json;
namespace Ndmspc {

///
/// \class NConfig
///
/// \brief NConfig object
///	\author Martin Vala <mvala@cern.ch>
///

class NConfig : public TNamed {
  public:
  NConfig();
  NConfig(const std::string & config, const std::string & userConfig = "", const std::string & userConfigRaw = "");
  virtual ~NConfig();
  virtual void Print(Option_t * option = "") const;

  json & GetCfg() { return fCfg; }
  bool   Load(const std::string & config, const std::string & userConfig = "", const std::string & userConfigRaw = "");
  bool   IsLoaded() { return fCfgLoaded; }

  std::string              GetEnvironment() const;
  bool                     SetEnvironment(const std::string & environment = "");
  std::string              GetAnalysisBasePath() const;
  std::string              GetWorkspacePath() const;
  std::string              GetAnalysisInputPath() const;
  std::string              GetAnalysisName() const;
  std::string              GetAnalysisRevision() const;
  std::string              GetAnalysisNameRevision() const;
  std::string              GetAnalysisPath() const;
  std::string              GetInputPrefix() const;
  std::string              GetInputObjectDirecotry() const;
  std::vector<std::string> GetInputObjectNames() const;
  std::string              GetMapFileName() const;
  std::string              GetMapObjectName() const;

  // Delete copy constructor and assignment operator
  NConfig(const NConfig &)             = delete;
  NConfig & operator=(const NConfig &) = delete;

  // Static method to get the instance
  static NConfig * Instance(const std::string & config = "", const std::string & userConfig = "",
                            const std::string & userConfigRaw = "")
  {

    std::lock_guard<std::mutex> lock(fMutex);
    if (fgInstance == nullptr) {
      if (config.empty()) {
        return nullptr;
      }
      fgInstance = std::unique_ptr<NConfig>(new NConfig(config, userConfig, userConfigRaw));
    }

    return fgInstance.get();
  }

  private:
  static std::unique_ptr<NConfig> fgInstance;
  static std::mutex               fMutex;
  bool                            fCfgLoaded{false}; ///< configuration loaded
  json                            fCfg;              ///< configuration
  /// \cond CLASSIMP
  ClassDef(NConfig, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
