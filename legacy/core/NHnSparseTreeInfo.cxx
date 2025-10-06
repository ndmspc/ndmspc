#include <TNamed.h>
#include "NLogger.h"
#include "NHnSparseTreeInfo.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseTreeInfo);
/// \endcond

namespace Ndmspc {
NHnSparseTreeInfo::NHnSparseTreeInfo(const char * name, const char * title) : TNamed(name, title)
{
  ///
  /// Constructor
  ///
}
NHnSparseTreeInfo::~NHnSparseTreeInfo()
{
  ///
  /// Destructor
  ///
}
void NHnSparseTreeInfo::Print(Option_t * option) const
{
  ///
  /// Print
  ///

  if (fHnSparseTree == nullptr) {
    NLogger::Error("HnSparseTree is not set !!!");
    return;
  }
  fHnSparseTree->Print(option);
}

} // namespace Ndmspc
