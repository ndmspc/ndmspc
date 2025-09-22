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

  if (fOutput == nullptr) {
    fOutput = new TList();
  }
  Long64_t entry = 0;
  if (fBinningDef) {
    entry = fBinningDef->GetId(coords[0]); // Get the binning definition ID for the first axis
    Ndmspc::NLogger::Debug("Binning definition ID: %lld", entry);
    fBinning->GetContent()->GetBinContent(entry,
                                          fBinning->GetPoint()->GetCoords()); // Get the bin content for the given entry
  }

  TList * outputPoint = new TList();
  fBinning->GetPoint()->RecalculateStorageCoords();
  fProcessFunc(fBinning->GetPoint(), fOutput, outputPoint, GetAssignedIndex());
  fTreeStorage->Fill(fBinning->GetPoint(), nullptr, {}, false);

  // if (outputPoint) delete outputPoint; // Clean up the output list
}

Long64_t NHnSparseThreadData::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///
  Long64_t nmerged = 0;

  NLogger::Debug("Merging thread data from %zu threads ...", list->GetEntries());

  TList * listOut = new TList();

  for (auto obj : *list) {
    if (obj->IsA() == NHnSparseThreadData::Class()) {
      NHnSparseThreadData * hnsttd = (NHnSparseThreadData *)obj;
      NLogger::Trace("Merging thread %zu", hnsttd->GetAssignedIndex());
      // hnsttd->GetOutput()->Print();

      if (fOutput == nullptr) {
        fOutput = hnsttd->GetOutput();
      }
      else {
        listOut->Add(hnsttd->GetOutput());
      }

      nmerged++;
    }
  }
  NLogger::Debug("Total entries to merge: %lld", nmerged);
  fOutput->Merge(listOut);
  // fOutput->Print();

  // TODO: Implement merging of fTreeStorage from all threads

  /// \cond CLASSIMP
  // NLogger::Error("NHnSparseThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}

bool NHnSparseThreadData::InitStorage()
{
  ///
  /// Initialize storage tree
  ///
  if (fTreeStorage) {
    NLogger::Warning("NHnSparseThreadData::InitStorage: Storage tree is already initialized !!!");
    return true;
  }

  if (fBinning == nullptr) {
    NLogger::Error("NHnSparseThreadData::InitStorage: Binning is not set !!!");
    return false;
  }

  fTreeStorage = new NStorageTree();
  // fTreeStorage->InitTree(Form("/tmp/hnst_thread_%zu.root", GetAssignedIndex()), "hnst");
  fTreeStorage->InitTree();

  return true;
}
} // namespace Ndmspc
