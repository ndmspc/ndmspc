#ifndef NdmspcCoreManager_H
#define NdmspcCoreManager_H
#include <TNamed.h>
namespace Ndmspc {

///
/// \class Manager
///
/// \brief Manager object
///	\author Martin Vala <mvala@cern.ch>
///
class InputMap;
class Results;
class Manager : public TNamed {

  public:
  Manager();
  virtual ~Manager();

  virtual void Print(Option_t * option = "") const;
  bool         Load(std::string config, std::string userConfig, std::string environment, std::string userConfigRaw,
                    std::string binning);

  private:
  Results * fResults{nullptr}; ///< result

  /// \cond CLASSIMP
  ClassDef(Manager, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
