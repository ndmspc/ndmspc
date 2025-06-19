#include <TROOT.h>
#include "NLogger.h"
#include "NHnSparseTreeThreadData.h"
#include <NHnSparseTreePoint.h>

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseTreeThreadData);
/// \endcond

namespace Ndmspc {
NHnSparseTreeThreadData::NHnSparseTreeThreadData() : NThreadData() {}
NHnSparseTreeThreadData::~NHnSparseTreeThreadData() {}
void NHnSparseTreeThreadData::Process(const std::vector<int> & coords)
{
  /// Process method
  /// This method is called for each set of coordinates
  /// It initializes the NHnSparseTree if not already done

  // NThreadData::Process(coords);

  if (fHnSparseTree == nullptr) GetHnSparseTree();
  {
    std::lock_guard<std::mutex> lock(fSharedMutex);
    fHnSparseTree->GetEntry(coords[0] - 1);
  }
  NHnSparseTreePoint * hnstPoint = fHnSparseTree->GetPoint();
  fProcessFunc(hnstPoint, GetAssignedIndex());
  // {
  //   std::lock_guard<std::mutex> lock(fSharedMutex);
  //   Longptr_t                   ok = gROOT->ProcessLine(
  //       TString::Format("%s((Ndmspc::NHnSparseTreePoint*)%p);", fMacro->GetName(), (void *)hnstPoint).Data());
  // }
}
NHnSparseTree * NHnSparseTreeThreadData::GetHnSparseTree()
{
  if (fHnSparseTree == nullptr) {
    {
      std::lock_guard<std::mutex> lock(fSharedMutex);
      fHnSparseTree = NHnSparseTree::Open(fFileName, fEnabledBranches);
      if (fHnSparseTree == nullptr) {
        NLogger::Error("Failed to open NHnSparseTree from file '%s'", fFileName.c_str());
      }
    }
  }

  return fHnSparseTree;
}
} // namespace Ndmspc
