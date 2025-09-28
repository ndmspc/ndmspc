#include <NHnSparseBase.h>
#include <NStorageTree.h>
#include <TList.h>
#include "THnSparse.h"
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"
#include "RtypesCore.h"
#include "NHnSparseThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseThreadData);
/// \endcond

namespace Ndmspc {
NHnSparseThreadData::NHnSparseThreadData() : NThreadData() {}
NHnSparseThreadData::~NHnSparseThreadData() {}
bool NHnSparseThreadData::Init(size_t id, NHnSparseProcessFuncPtr func, NHnSparseBase * hnsb, NBinning * hnsbBinningIn,
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

  if (hnsb == nullptr) {
    NLogger::Error("NHnSparseThreadData::Init: NHnSparseBase is nullptr !!!");
    return false;
  }
  fBiningSource = hnsbBinningIn;

  if (fBiningSource == nullptr) {
    NLogger::Error("NHnSparseThreadData::Init: Binning Source is nullptr !!!");
    return false;
  }

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

  for (auto & kv : ts->GetBranchesMap()) {
    NLogger::Debug("NHnSparseThreadData::Init: Adding branch '%s' to thread %zu", kv.first.c_str(), id);
    fHnSparseBase->GetStorageTree()->AddBranch(kv.first, nullptr, kv.second.GetObjectClassName());
  }

  // TODO: check if needed or move it somewhere else like Reset();
  //
  // Loop over aoll definitions and reset their content and ids
  for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
    NBinningDef * def = fHnSparseBase->GetBinning()->GetDefinition(name);
    if (def) {
      def->GetContent()->Reset();
      def->GetIds().clear();
    }
  }
  // fHnSparseBase->GetBinning()->GetDefinition()->GetContent()->Reset();
  // fHnSparseBase->GetBinning()->GetDefinition()->GetIds().clear();

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
  NBinningDef * binningDef = fBiningSource->GetDefinition();
  if (binningDef == nullptr) {
    NLogger::Error("NHnSparseThreadData::Process: Binning definition is not set in NHnSparseThreadData !!!");
    return;
  }

  NStorageTree * ts = fHnSparseBase->GetStorageTree();

  NBinningPoint * point = fHnSparseBase->GetBinning()->GetPoint();

  Long64_t entry = fBiningSource->GetDefinition()->GetId(coords[0]);

  if (entry < fHnSparseBase->GetBinning()->GetContent()->GetNbins()) {
    fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(entry);
    // entry = binningDef->GetContent()->GetBinContent(entry);
    // point->SetEntryNumber(entry);
    NLogger::Debug("Skipping entry=%lld, because it was already process !!!", entry);
    return;
  }

  // Long64_t        entry = binningDef->GetId(coords[0]);
  Ndmspc::NLogger::Debug("NHnSparseThreadData::Process: Entry in global content mapping: %lld", entry);
  fBiningSource->GetContent()->GetBinContent(entry, point->GetCoords());
  point->RecalculateStorageCoords(entry, false);
  point->SetCfg(fCfg); // Set configuration to the point
  // point->Print("C");

  // TODO: check if entry was already processed
  // So we dont execute the function again

  // NLogger::Debug(
  //     "AAA NHnSparseThreadData::Process: Thread %zu processing entry %lld for coordinates %s", GetAssignedIndex(),
  //     entry,
  //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
  TList * outputPoint = new TList();
  fProcessFunc(point, fHnSparseBase->GetOutput(), outputPoint, GetAssignedIndex());
  Ndmspc::NLogger::Trace(
      "NHnSparseThreadData::Process: thread=%zu entry=%lld coords=%s outputPoint=%d", GetAssignedIndex(), entry,
      NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str(),
      outputPoint->GetEntries());
  if (outputPoint->GetEntries() > 0) {
    ts->GetBranch("outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
    // ts->Fill(point, nullptr, false, {}, false);
    Int_t bytes = ts->Fill(point, nullptr, false, {}, false);
    if (bytes > 0) {
      // Long64_t entryInBinDef = binningDefgcc->GetId(coords[0]);
      // NLogger::Debug("NHnSparseThreadData::Process: Thread %zu: Filled %d bytes for coordinates %s entry=%lld",
      //                GetAssignedIndex(), bytes, NUtils::GetCoordsString(coords).c_str(), entry);
      fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(entry);
    }
    else {
      // NLogger::Error("NHnSparseThreadData::Process: Thread %zu: zero bytes were writtent for coordinates %s
      // entry=%lld",
      //                GetAssignedIndex(), NUtils::GetCoordsString(coords).c_str(), entry);
    }
    // outputPoint->Print();
    outputPoint->Clear(); // Clear the list to avoid memory leaks
  }
  else {
    NLogger::Trace(
        "No output for coordinates %s",
        NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
  }

  delete outputPoint; // Clean up the output list
}

Long64_t NHnSparseThreadData::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///
  Long64_t nmerged = 0;

  NLogger::Debug("------------------------------------------------");
  NLogger::Debug("Merging thread data from %zu threads ...", list->GetEntries());

  NStorageTree *                 ts = nullptr;
  std::map<std::string, TList *> listOutputs;

  TList * listOut         = new TList();
  TList * listTreeStorage = new TList();

  for (auto obj : *list) {
    if (obj->IsA() == NHnSparseThreadData::Class()) {
      NHnSparseThreadData * hnsttd = (NHnSparseThreadData *)obj;
      NLogger::Debug("Merging thread %zu processed %lld", hnsttd->GetAssignedIndex(), hnsttd->GetNProcessed());
      ts = hnsttd->GetHnSparseBase()->GetStorageTree();
      if (!ts) {
        NLogger::Error("NHnSparseThreadData::Merge: Storage tree is not set in NHnSparseBase !!!");
        continue;
      }
      hnsttd->Print();

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

      fHnSparseBase->GetBinning()->GetContent()->Reset();
      // Print hnsb binning definition ids
      for (const auto & name : hnsb->GetBinning()->GetDefinitionNames()) {
        NBinningDef * binningDef = hnsb->GetBinning()->GetDefinition(name);
        if (!binningDef) {
          NLogger::Error("Binning definition '%s' not found in NHnSparseBase !!!", name.c_str());
          continue;
        }
        NBinningDef * targetBinningDef = fHnSparseBase->GetBinning()->GetDefinition(name);
        binningDef->Print();
        // NLogger::Trace("Binning definition '%s' has %zu ids", name.c_str(), binningDef->GetIds().size());
        // NLogger::Trace("  IDs: %s", NUtils::GetCoordsString(binningDef->GetIds(), -1).c_str());
        // targetBinningDef->GetIds().clear();
        targetBinningDef->GetIds().insert(targetBinningDef->GetIds().end(), binningDef->GetIds().begin(),
                                          binningDef->GetIds().end());
        // Sort fHnSparseBase->GetBinning()->GetDefinition()->GetIds()
        std::sort(targetBinningDef->GetIds().begin(), targetBinningDef->GetIds().end());

        // targetBinningDef->GetIds().erase(
        //     std::unique(targetBinningDef->GetIds().begin(), targetBinningDef->GetIds().end()),
        //     targetBinningDef->GetIds().end());

        targetBinningDef->Print();

        // NLogger::Trace("  IDs: %s",
        //                NUtils::GetCoordsString(fHnSparseBase->GetBinning()->GetDefinition()->GetIds(), -1).c_str());
        for (auto id : fHnSparseBase->GetBinning()->GetDefinition(name)->GetIds()) {
          NBinningPoint point(fHnSparseBase->GetBinning());
          fBiningSource->GetContent()->GetBinContent(id, point.GetCoords());
          Long64_t bin = fHnSparseBase->GetBinning()->GetContent()->GetBin(point.GetCoords());
          fHnSparseBase->GetBinning()->GetContent()->SetBinContent(bin, 1);
        }
      }

      // TODO: check if needed
      // fHnSparseBase->GetBinning()->GetDefinition()->GetContent()->Add(
      //     hnsb->GetBinning()->GetDefinition()->GetContent());
      nmerged++;
    }
  }
  NLogger::Debug("Total entries to merge: %lld", nmerged);

  NLogger::Debug("Merging %d storage trees ...", listTreeStorage->GetEntries());
  // fHnSparseBase->GetBinning()->GetPoint()->Reset();
  fHnSparseBase->GetStorageTree()->Merge(listTreeStorage);

  // loop over all output lists and merge them
  for (auto & kv : listOutputs) {
    if (kv.second && !kv.second->IsEmpty()) {
      NLogger::Debug("Merging output list '%s' with %d objects", kv.first.c_str(), kv.second->GetEntries() + 1);
      fHnSparseBase->GetOutput(kv.first)->Merge(kv.second);
      fHnSparseBase->GetOutput(kv.first)->Print();
    }
  }

  // Loop over binning definitions and merge their contents
  fHnSparseBase->GetStorageTree()->SetEnabledBranches({}, 0);
  for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
    NBinningDef * binningDef = fHnSparseBase->GetBinning()->GetDefinition(name);
    if (!binningDef) {
      NLogger::Error("Binning definition '%s' not found in NHnSparseBase !!!", name.c_str());
      continue;
    }
    // Recalculate binningDef content based on ids
    binningDef->GetContent()->Reset();
    for (auto id : binningDef->GetIds()) {
      fHnSparseBase->GetEntry(id, false);
      Long64_t bin = binningDef->GetContent()->GetBin(fHnSparseBase->GetBinning()->GetPoint()->GetStorageCoords());
      binningDef->GetContent()->SetBinContent(bin, id);
    }
  }

  // print all definitions
  for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
    fHnSparseBase->GetBinning()->GetDefinition(name)->Print();
  }

  // Set default setting
  fHnSparseBase->GetBinning()->GetPoint()->Reset();
  fHnSparseBase->GetStorageTree()->SetEnabledBranches({}, 1);
  fHnSparseBase->GetBinning()->SetCurrentDefinitionName(fHnSparseBase->GetBinning()->GetDefinitionNames().front());

  /// \cond CLASSIMP
  // NLogger::Error("NHnSparseThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}
} // namespace Ndmspc
