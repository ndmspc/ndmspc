#include <TNamed.h>
#include "Logger.h"
#include "HnSparseTreeInfo.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::HnSparseTreeInfo);
/// \endcond

namespace Ndmspc {
HnSparseTreeInfo::HnSparseTreeInfo(const char * name, const char * title) : TNamed(name, title)
{
  ///
  /// Constructor
  ///
}
HnSparseTreeInfo::~HnSparseTreeInfo()
{
  ///
  /// Destructor
  ///
}
void HnSparseTreeInfo::Print(Option_t * option) const
{
  ///
  /// Print
  ///
  auto logger = Ndmspc::Logger::getInstance("");

  if (fHnSparseTree == nullptr) {
    logger->Error("HnSparseTree is not set !!!");
    return;
  }
  fHnSparseTree->Print(option);
}

} // namespace Ndmspc
