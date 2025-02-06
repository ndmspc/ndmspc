#ifndef NdmspcCore_H
#define NdmspcCore_H
#include <TObject.h>
#include "Utils.h"

namespace Ndmspc {

/// Ndmspc global configuration
extern json gCfg;

///
/// \class Core
///
/// \brief Core object
///	\author Martin Vala <mvala@cern.ch>
///

class Core : public TObject {
  Core() {};
  virtual ~Core() {};

  public:
  static bool LoadConfig(std::string config, std::string userConfig, std::string environment = "",
                         std::string userConfigRaw = "");
  static bool LoadEnvironment(std::string environmenti = "local");

  static bool SaveConfig(json cfg, std::string filename);

  private:
  /// \cond CLASSIMP
  ClassDef(Core, 0);
  /// \endcond;
};
} // namespace Ndmspc
#endif
