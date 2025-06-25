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

  if (fHnstIn == nullptr) GetHnstInput();
  if (fHnstOut == nullptr) GetHnstOutput();
  {
    std::lock_guard<std::mutex> lock(fSharedMutex);
    fHnstIn->GetEntry(coords[0] - 1);
  }
  NHnSparseTreePoint * hnstPoint = fHnstIn->GetPoint();
  TList *              output    = new TList();

  output->SetOwner(true); // Set owner to true to delete objects in the list automatically
  fProcessFunc(hnstPoint, output, GetAssignedIndex());
  // output->Print();
  if (fHnstOut) {
    std::lock_guard<std::mutex> lock(fSharedMutex);
    NLogger::Trace("Saving output for thread %d %p", GetAssignedIndex(), (void *)fHnstOut->GetBranch("output"));

    fHnstOut->GetBranch("output")->SetAddress(output); // Set the output list as branch address
    std::vector<int> coordsOut = hnstPoint->GetPointContent();
    coordsOut.push_back(1);
    fHnstOut->GetPoint()->SetPointContent(coordsOut); // Set the point in the current HnSparseTree

    fHnstOut->SaveEntry();
  }
  delete output;
}
NHnSparseTree * NHnSparseTreeThreadData::GetHnstInput()
{
  if (fHnstIn == nullptr) {
    {
      std::lock_guard<std::mutex> lock(fSharedMutex);
      fHnstIn = NHnSparseTree::Open(fFileName, fEnabledBranches);
      if (fHnstIn == nullptr) {
        NLogger::Error("Failed to open NHnSparseTree from file '%s'", fFileName.c_str());
      }
    }
  }

  return fHnstIn;
}
NHnSparseTree * NHnSparseTreeThreadData::GetHnstOutput(NHnSparseTree * hnstOut)
{
  if (fHnstOut == nullptr) {
    {
      std::lock_guard<std::mutex> lock(fSharedMutex);

      std::string tmpFileName = "/tmp/test/hnst_out_" + std::to_string(GetAssignedIndex()) + ".root";
      NLogger::Trace("Creating output NHnSparseTree in file '%s'", tmpFileName.c_str());

      if (hnstOut == nullptr) {
        fHnstOut = new NHnSparseTreeC(tmpFileName);
      }
      else {
        fHnstOut = (NHnSparseTree *)hnstOut->Clone();
        fHnstOut->SetFileName(tmpFileName);
        fHnstOut->InitTree();
        // fHnstOut->InitAxes(fHnstOut->GetBinning()->GetAxes(), 0);
        // fHnstOut->ImportBinning(hnstOut->GetBinning()->GetDefinition());
        fHnstOut->GetBinning()->GetContent()->Reset();
        fHnstOut->GetBinning()->GetMap()->Reset();
        // fHnstOut->Print();
      }
      if (fHnstOut == nullptr) {
        NLogger::Error("Failed to open NHnSparseTree file '%s' for writing !!!", tmpFileName.c_str());
      }
      fHnstOut->AddBranch("output", nullptr, "TList");
    }
  }

  return fHnstOut;
}
Long64_t NHnSparseTreeThreadData::Merge(TCollection * list)
{
  /// Merge method
  /// This method merges the output of the threads into a single NHnSparseTree

  NLogger::Trace("Merging thread data from %zu threads ...", list->GetEntries());

  Long64_t totalEntries = 0;
  TList *  listOut      = new TList();

  for (auto obj : *list) {
    if (obj->IsA() == NHnSparseTreeThreadData::Class()) {
      NHnSparseTreeThreadData * hnsttd = (NHnSparseTreeThreadData *)obj;
      NLogger::Trace("Processing thread %zu with file '%s'", hnsttd->GetAssignedIndex(), hnsttd->GetFileName().c_str());

      // TODO: Handle case where partial resutls are not closed before merging
      // NHnSparseTree * hnstOut = hnsttd->GetHnstOutput();
      NHnSparseTree * hnstOut = nullptr;
      if (hnstOut == nullptr)
        hnstOut = NHnSparseTree::Open(hnsttd->GetHnstOutput()->GetFileName());
      else {
        NLogger::Debug("Using existing output NHnSparseTree for thread %zu", hnsttd->GetAssignedIndex());
        // hnstOut->SetBranchAddresses();
      }
      if (hnstOut == nullptr) {
        NLogger::Error("NHnSparseTreeThreadData::Merge: Output NHnSparseTree is nullptr for thread %zu",
                       hnsttd->GetAssignedIndex());
        continue;
      }

      NLogger::Trace("Going to merge thread %zu with file '%s' entries=%lld", hnsttd->GetAssignedIndex(),
                     hnstOut->GetFileName().c_str(), hnstOut->GetTree()->GetEntries());
      listOut->Add(hnstOut);

      totalEntries++;
    }
  }
  NLogger::Trace("Total entries to merge: %lld", totalEntries);
  fHnstOut->Merge(listOut);

  // TODO: Delete teporary files
  // for (auto obj : *listOut) {
  //   NHnSparseTree * hnst = (NHnSparseTree *)obj;
  //   NLogger::Debug("Deleting temporary file '%s' ...", hnst->GetFileName().c_str());
  //   NUtils::DeleteFile(hnst->GetFileName());
  // }
  return totalEntries;
}
} // namespace Ndmspc
