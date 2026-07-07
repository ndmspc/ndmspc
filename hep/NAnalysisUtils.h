#ifndef Ndmspc_AnalysisUtils_H
#define Ndmspc_AnalysisUtils_H
#include <TObject.h>
#include <TH1.h>
#include <TF1.h>
#include "NLogger.h"

namespace Ndmspc {

///
/// \class NAnalysisUtils
///
/// \brief NAnalysisUtils object
///	\author Martin Vala <mvala@cern.ch>
///

class NAnalysisUtils : public TObject {
  public:
  // NAnalysisUtils();
  // virtual ~NAnalysisUtils();

  static bool ExtractSignal(TH1 * sigBg, TH1 * bg, TF1 * fitFunc, json & cfg, TList * output = nullptr,
                            TH1 * results = nullptr);

  static bool ExtractSignalRooFit(TH1 * sigBg, TH1 * bg, json & cfg, TList * output = nullptr, TH1 * results = nullptr);

  static void ProcessSystematics(const std::string & cfgFile);

  static void ResetHistograms(TList * list);

  /// \cond CLASSIMP
  ClassDef(NAnalysisUtils, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
