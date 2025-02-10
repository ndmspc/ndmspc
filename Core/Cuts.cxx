#include <TString.h>
#include "Cuts.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Cuts);
/// \endcond

namespace Ndmspc {
Cuts::Cuts() : TObject()
{
  ///
  /// Constructor
  ///
}
Cuts::~Cuts()
{
  ///
  /// Destructor
  ///
}
void Cuts::Print(Option_t * option) const
{
  /// Print cuts info

  for (auto axis : fAxes) {
    axis->Print(option);
  }
}
} // namespace Ndmspc
