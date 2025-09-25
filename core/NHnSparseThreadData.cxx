#include <NHnSparseBase.h>
#include <NStorageTree.h>
#include <TList.h>
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NHnSparseThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseThreadData);
/// \endcond

namespace Ndmspc {
NHnSparseThreadData::NHnSparseThreadData() : NThreadData() {}
NHnSparseThreadData::~NHnSparseThreadData() {}
bool NHnSparseThreadData::Init(size_t id, NHnSparseProcessFuncPtr func, NHnSparseBase * hnsb,
                               const std::string & filename, const std::string & treename)
{
  ///
  /// Initialize thread data
  ///
  SetAssignedIndex(id);
  SetThreadId(std::this_thread::get_id());
  // SetIdSet(true);
  //
  TH1::AddDirectory(kFALSE); // Disable ROOT auto directory management

  // if (!func) {
  //   NLogger::Error("NHnSparseThreadData::Init: Process function is not set !!!");
  //   return false;
  // }
  fProcessFunc = func;

  if (!hnsb) {
    NLogger::Error("NHnSparseThreadData::Init: NHnSparseBase is not set !!!");
    return false;
  }
  fBiningSource = hnsb->GetBinning();
  fHnSparseBase = (NHnSparseBase *)hnsb->Clone();
  // fHnSparseBase = new NHnSparseBase(hnsb->GetBinning(), (NStorageTree *)hnsb->GetStorageTree()->Clone());
  // fHnSparseBase = new NHnSparseBase(hnsb->GetBinning(), new NStorageTree(hnsb->GetBinning()));
  // fHnSparseBase = new NHnSparseBase(hnsb->GetBinning(), nullptr);

  if (fHnSparseBase->GetBinning() == nullptr) {
    NLogger::Error("NHnSparseThreadData::InitStorage: Binning is not set !!!");
    return false;
  }

  if (fHnSparseBase->GetStorageTree() == nullptr) {
    NLogger::Error("NHnSparseThreadData::InitStorage: Storage tree is not set !!!");
    return false;
  }

  NStorageTree * ts = hnsb->GetStorageTree();
  fHnSparseBase->GetStorageTree()->Clear("F");
  fHnSparseBase->GetStorageTree()->InitTree(filename.empty() ? ts->GetFileName() : filename, treename);
  // Recreate the point and set the storage tree
  fHnSparseBase->GetBinning()->GetPoint()->SetTreeStorage(fHnSparseBase->GetStorageTree());
  fHnSparseBase->GetBinning()->GetPoint()->SetCfg(fBiningSource->GetPoint()->GetCfg());

  for (auto & kv : ts->GetBranchesMap()) {
    NLogger::Debug("NHnSparseThreadData::Init: Adding branch '%s' to thread %zu", kv.first.c_str(), id);
    fHnSparseBase->GetStorageTree()->AddBranch(kv.first, nullptr, kv.second.GetObjectClassName());
  }

  return true;
}

void NHnSparseThreadData::Process(const std::vector<int> & coords)
{
  /// Process method
  /// This method is called for each set of coordinates
  /// It initializes the NHnSparseTree if not already done

  fNProcessed++;
  // NThreadData::Process(coords);

  // NLogger::Debug("NHnSparseThreadData::Process: Thread %d processing coordinates %s", GetAssignedIndex(),
  //                NUtils::GetCoordsString(coords).c_str());

  if (!fHnSparseBase) {
    NLogger::Error("NHnSparseThreadData::Process: NHnSparseBase is not set in NHnSparseThreadData !!!");
    return;
  }

  if (!fProcessFunc) {
    NLogger::Error("NHnSparseThreadData::Process: Process function is not set in NHnSparseThreadData !!!");
    return;
  }

  // NBinning *     binning    = fBiningSource;
  NBinningDef *  binningDef = fBiningSource->GetDefinition();
  NStorageTree * ts         = fHnSparseBase->GetStorageTree();

  Long64_t entry = 0;
  // {
  //   std::lock_guard<std::mutex> lock(fSharedMutex);
  if (binningDef) {
    entry = binningDef->GetId(coords[0]); // Get the binning definition ID for the first axis
    // Ndmspc::NLogger::Debug("Entry: %lld", entry);
    fBiningSource->GetContent()->GetBinContent(
        entry,
        fHnSparseBase->GetBinning()->GetPoint()->GetCoords()); // Get the bin content for the given entry
    fHnSparseBase->GetBinning()->GetPoint()->RecalculateStorageCoords(entry, false);
  }
  else {
    NLogger::Error("NHnSparseThreadData::Process: Binning definition is not set in NHnSparseBase !!!");
    return;
  }
  // return;
  // fHnSparseBase->GetBinning()->GetPoint()->Print();

  TList * outputPoint = new TList();
  fProcessFunc(fHnSparseBase->GetBinning()->GetPoint(), fHnSparseBase->GetOutput(), outputPoint, GetAssignedIndex());

  // outputPoint->Print();
  if (!outputPoint->IsEmpty()) {
    ts->GetBranch("output")->SetAddress(outputPoint); // Set the output list as branch address
    ts->Fill(fHnSparseBase->GetBinning()->GetPoint(), nullptr, {}, false);
    outputPoint->Clear(); // Clear the list to avoid memory leaks
  }

  delete outputPoint; // Clean up the output list
}

Long64_t NHnSparseThreadData::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///
  Long64_t nmerged = 0;

  NLogger::Debug("Merging thread data from %zu threads ...", list->GetEntries());

  NStorageTree *                 ts = nullptr;
  std::map<std::string, TList *> listOutputs;

  TList * listOut         = new TList();
  TList * listTreeStorage = new TList();

  for (auto obj : *list) {
    if (obj->IsA() == NHnSparseThreadData::Class()) {
      NHnSparseThreadData * hnsttd = (NHnSparseThreadData *)obj;
      NLogger::Debug("Merging thread %zu processed %lld", hnsttd->GetAssignedIndex(), hnsttd->GetNProcessed());
      // hnsttd->GetOutput()->Print();
      ts = hnsttd->GetHnSparseBase()->GetStorageTree();
      if (!ts) {
        NLogger::Error("NHnSparseThreadData::Merge: Storage tree is not set in NHnSparseBase !!!");
        continue;
      }

      for (auto & kv : hnsttd->GetHnSparseBase()->GetOutputs()) {
        NLogger::Debug("Found output list '%s' with %d objects", kv.first.c_str(),
                       kv.second ? kv.second->GetEntries() : 0);
        if (kv.second && !kv.second->IsEmpty()) {
          NLogger::Debug("Merging output list '%s' with %d objects", kv.first.c_str(), kv.second->GetEntries());
          if (listOutputs.find(kv.first) == listOutputs.end()) {
            if (fHnSparseBase->GetOutput(kv.first)->IsEmpty()) {
              listOutputs[kv.first] = new TList();
              fHnSparseBase->GetOutput(kv.first)->AddAll(kv.second);
            }
          }
          else {
            listOutputs[kv.first]->Add(kv.second);
          }
        }
      }

      // if (fOutput == nullptr) {
      //   fOutput = hnsttd->GetOutput();
      // }
      // else {
      //   listOut->Add(hnsttd->GetOutput());
      // }

      NHnSparseBase * hnsb = NHnSparseBase::Open(ts->GetFileName());
      if (!hnsb) {
        NLogger::Error("NHnSparseThreadData::Merge: Failed to open NHnSparseBase from file '%s' !!!",
                       ts->GetFileName().c_str());
        continue;
      }
      // hnsb->Print();
      listTreeStorage->Add(hnsb->GetStorageTree());

      nmerged++;
    }
  }
  NLogger::Debug("Total entries to merge: %lld", nmerged);

  fHnSparseBase->GetStorageTree()->Merge(listTreeStorage);

  // loop over all output lists and merge them
  for (auto & kv : listOutputs) {
    if (kv.second && !kv.second->IsEmpty()) {
      NLogger::Debug("Merging output list '%s' with %d objects", kv.first.c_str(), kv.second->GetEntries() + 1);
      fHnSparseBase->GetOutput(kv.first)->Merge(kv.second);
      fHnSparseBase->GetOutput(kv.first)->Print();
    }
  }

  /// \cond CLASSIMP
  // NLogger::Error("NHnSparseThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}
} // namespace Ndmspc
