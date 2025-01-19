#include <TString.h>
#include "Cuts.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Cuts);
/// \endcond

namespace Ndmspc {
Cuts::Cuts() : TObject() {}
Cuts::~Cuts() {}
void Cuts::Print(Option_t * option) const
{

  for (auto axis : fAxes) {
    axis->Print(option);
  }
}
} // namespace Ndmspc
