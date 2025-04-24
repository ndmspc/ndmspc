#ifndef Ndmspc_AliceUtils_H
#define Ndmspc_AliceUtils_H
#include <set>
#include <nlohmann/json.hpp>
#include <TObject.h>
#include "Utils.h"

using json = nlohmann::json;

namespace Ndmspc {

///
/// \class AliceUtils
///
/// \brief AliceUtils object
///	\author Martin Vala <mvala@cern.ch>
///

class AliceUtils : public TObject {
  public:
  AliceUtils() : TObject() {}
  virtual ~AliceUtils() {}

  static std::string QueryTrainInfo(int id);
  static bool        InputsFromHyperloop(std::string config, std::set<int> ids, std::string analysis = "PWGXX-YYY");
  static void        Import(std::string filename =
                                "alien:///alice/cern.ch/user/a/alihyperloop/outputs/0036/360353/83976/AnalysisResults.root",
                            std::string analysis = "PWGXX-YYY", std::string production = "LHCXXX", bool mc = false,
                            Long64_t hyperloopId = -1, std::string resultsFilename = "",
                            std::string base    = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/dev/alice",
                            std::string postfix = "inputs");

  /// \cond CLASSIMP
  ClassDef(AliceUtils, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
