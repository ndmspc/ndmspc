#ifndef Ndmspc_AnalysisUtils_H
#define Ndmspc_AnalysisUtils_H
#include <TObject.h>

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

  static bool Init();

  /// \cond CLASSIMP
  ClassDef(AnalysisUtils, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
