#ifndef Ndmspc_AnalysisUtils_H
#define Ndmspc_AnalysisUtils_H
#include <TObject.h>
#include <TH1.h>
#include <TF1.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Ndmspc {

///
/// \class AnalysisUtils
///
/// \brief AnalysisUtils object
///	\author Martin Vala <mvala@cern.ch>
///

class AnalysisUtils : public TObject {
  public:
  // AnalysisUtils();
  // virtual ~AnalysisUtils();

  static bool ExtractSignal(TH1 * sigBg, TH1 * bg, TF1 * fitFunc, json & cfg, TList * output = nullptr,
                            TH1 * results = nullptr);
  static void ResetHistograms(TList * list);

  /// \cond CLASSIMP
  ClassDef(AnalysisUtils, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
