#include "NHnSparsePoint.h"
#include "NLogger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparsePoint);
/// \endcond

namespace Ndmspc {
NHnSparsePoint::NHnSparsePoint(NGnTree * in, NGnTree * out) : TObject(), fIn(in), fOut(out) {}
NHnSparsePoint::~NHnSparsePoint() {}
void NHnSparsePoint::Print(Option_t * option) const
{
  /// Print object
  ///

  NLogInfo("NHnSparsePoint: %s", option);
}
} // namespace Ndmspc
