#include <TList.h>
#include "NLogger.h"
#include "NUtils.h"
#include "NHnSparseThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseThreadData);
/// \endcond

namespace Ndmspc {
NHnSparseThreadData::NHnSparseThreadData() : NThreadData() {}
NHnSparseThreadData::~NHnSparseThreadData() {}

void NHnSparseThreadData::Process(const std::vector<int> & coords)
{
  /// Process method
  /// This method is called for each set of coordinates
  /// It initializes the NHnSparseTree if not already done

  // NThreadData::Process(coords);

  NLogger::Debug("NHnSparseThreadData::Process: Thread %d processing coordinates %s", GetAssignedIndex(),
                 NUtils::GetCoordsString(coords).c_str());

  if (!fProcessFunc) {
    NLogger::Error("Process function is not set in NHnSparseThreadData !!!");
    return;
  }

  if (fOutputGlobal == nullptr) {
    fOutputGlobal = new TList();
  }

  TList * output = new TList();
  fProcessFunc(nullptr, output, fOutputGlobal, GetAssignedIndex());
  delete output; // Clean up the output list
}

Long64_t NHnSparseThreadData::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///
  Long64_t nmerged = 0;
  /// \cond CLASSIMP
  NLogger::Error("NHnSparseThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}
} // namespace Ndmspc
