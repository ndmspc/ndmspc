#include "NHnSparsePoint.h"
#include "NLogger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparsePoint);
/// \endcond

namespace Ndmspc {
NHnSparsePoint::NHnSparsePoint(NHnSparseBase * in, NHnSparseBase * out) : TObject(), fIn(in), fOut(out) {}
NHnSparsePoint::~NHnSparsePoint() {}
void NHnSparsePoint::Print(Option_t * option) const
{
  /// Print object
  ///

  NLogger::Info("NHnSparsePoint: %s", option);
}
} // namespace Ndmspc
