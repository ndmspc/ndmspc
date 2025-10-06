#include <NGnTree.h>
#include <NStorageTree.h>
#include <TList.h>
#include "THnSparse.h"
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"
#include "RtypesCore.h"
#include "NGnThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnThreadData);
/// \endcond

namespace Ndmspc {
NGnThreadData::NGnThreadData() : NThreadData() {}
NGnThreadData::~NGnThreadData() {}
bool NGnThreadData::Init(size_t id, NHnSparseProcessFuncPtr func, NGnTree * hnsb, NBinning * hnsbBinningIn,
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
  //   NLogger::Error("NGnThreadData::Init: Process function is not set !!!");
  //   return false;
  // }
  fProcessFunc = func;

  if (hnsb == nullptr) {
    NLogger::Error("NGnThreadData::Init: NGnTree is nullptr !!!");
    return false;
  }
  fBiningSource = hnsbBinningIn;

  if (fBiningSource == nullptr) {
    NLogger::Error("NGnThreadData::Init: Binning Source is nullptr !!!");
    return false;
  }

  fHnSparseBase = (NGnTree *)hnsb->Clone();
  // fHnSparseBase = new NGnTree(hnsb->GetBinning(), (NStorageTree *)hnsb->GetStorageTree()->Clone());
  // fHnSparseBase = new NGnTree(hnsb->GetBinning(), new NStorageTree(hnsb->GetBinning()));
  // fHnSparseBase = new NGnTree(hnsb->GetBinning(), nullptr);

  if (fHnSparseBase->GetBinning() == nullptr) {
    NLogger::Error("NGnThreadData::InitStorage: Binning is not set !!!");
    return false;
  }

  if (fHnSparseBase->GetStorageTree() == nullptr) {
    NLogger::Error("NGnThreadData::InitStorage: Storage tree is not set !!!");
    return false;
  }

  NStorageTree * ts = hnsb->GetStorageTree();
  fHnSparseBase->GetStorageTree()->Clear("F");
  fHnSparseBase->GetStorageTree()->InitTree(filename.empty() ? ts->GetFileName() : filename, treename);
  // Recreate the point and set the storage tree
  fHnSparseBase->GetBinning()->GetPoint()->SetTreeStorage(fHnSparseBase->GetStorageTree());

  for (auto & kv : ts->GetBranchesMap()) {
    NLogger::Trace("NGnThreadData::Init: Adding branch '%s' to thread %zu", kv.first.c_str(), id);
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

void NGnThreadData::Process(const std::vector<int> & coords)
{
  /// Process method
  /// This method is called for each set of coordinates
  /// It initializes the NHnSparseTree if not already done

  fNProcessed++;
  // NThreadData::Process(coords);

  // NLogger::Debug("NGnThreadData::Process: Thread %d processing coordinates %s", GetAssignedIndex(),
  //                NUtils::GetCoordsString(coords).c_str());

  if (!fHnSparseBase) {
    NLogger::Error("NGnThreadData::Process: NGnTree is not set in NGnThreadData !!!");
    return;
  }

  if (!fProcessFunc) {
    NLogger::Error("NGnThreadData::Process: Process function is not set in NGnThreadData !!!");
    return;
  }

  // NBinning *     binning    = fBiningSource;
  NBinningDef * binningDef = fBiningSource->GetDefinition();
  if (binningDef == nullptr) {
    NLogger::Error("NGnThreadData::Process: Binning definition is not set in NGnThreadData !!!");
    return;
  }

  NStorageTree * ts = fHnSparseBase->GetStorageTree();

  NBinningPoint * point = fHnSparseBase->GetBinning()->GetPoint();

  Long64_t entry = fBiningSource->GetDefinition()->GetId(coords[0]);

  if (entry < fHnSparseBase->GetBinning()->GetContent()->GetNbins()) {
    fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(entry);
    // entry = binningDef->GetContent()->GetBinContent(entry);
    // point->SetEntryNumber(entry);
    NLogger::Debug("NGnThreadData::Process: [%zu] Skipping entry=%lld, because it was already process !!!",
                   GetAssignedIndex(), entry);
    return;
  }

  // Long64_t        entry = binningDef->GetId(coords[0]);
  // Ndmspc::NLogger::Debug("NGnThreadData::Process: [%zu] Entry in global content mapping: %lld",
  //                        GetAssignedIndex(), entry);
  fBiningSource->GetContent()->GetBinContent(entry, point->GetCoords());
  point->RecalculateStorageCoords(entry, false);
  point->SetCfg(fCfg); // Set configuration to the point
  // point->Print("C");

  // TODO: check if entry was already processed
  // So we dont execute the function again

  // NLogger::Debug(
  //     "AAA NGnThreadData::Process: Thread %zu processing entry %lld for coordinates %s", GetAssignedIndex(),
  //     entry,
  //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
  TList * outputPoint = new TList();
  fProcessFunc(point, fHnSparseBase->GetOutput(), outputPoint, GetAssignedIndex());
  // Ndmspc::NLogger::Trace(
  //     "NGnThreadData::Process: [%zu] entry=%lld coords=%s outputPoint=%d", GetAssignedIndex(), entry,
  //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str(),
  //     outputPoint->GetEntries());
  if (outputPoint->GetEntries() > 0) {
    Ndmspc::NLogger::Trace(
        "NGnThreadData::Process: [%zu] Entry '%lld' was accepted. %s", GetAssignedIndex(), entry,
        NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());

    ts->GetBranch("outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
    // ts->Fill(point, nullptr, false, {}, false);
    Int_t bytes = ts->Fill(point, nullptr, false, {}, false);
    if (bytes > 0) {
      // Long64_t entryInBinDef = binningDefgcc->GetId(coords[0]);
      // NLogger::Debug("NGnThreadData::Process: Thread %zu: Filled %d bytes for coordinates %s entry=%lld",
      //                GetAssignedIndex(), bytes, NUtils::GetCoordsString(coords).c_str(), entry);

      fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(entry);
      // NLogger::Info("Entry number in storage tree: %lld", point->GetEntryNumber());
      // fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(point->GetEntryNumber());
    }
    else {
      Ndmspc::NLogger::Trace(
          "NGnThreadData::Process: [%zu] Entry '%lld' Fill was done with 0 bytes. Skipping ...",
          GetAssignedIndex(), entry);
      // NLogger::Error("NGnThreadData::Process: Thread %zu: zero bytes were writtent for coordinates %s
      // entry=%lld",
      //                GetAssignedIndex(), NUtils::GetCoordsString(coords).c_str(), entry);
    }
    // outputPoint->Print();
    outputPoint->Clear(); // Clear the list to avoid memory leaks
  }
  else {
    Ndmspc::NLogger::Trace(
        "NGnThreadData::Process: [%zu] Entry '%lld' No output %s. Skipping ...", GetAssignedIndex(), entry,
        NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
    // NLogger::Trace(
    //     "No output for coordinates %s",
    //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
  }

  delete outputPoint; // Clean up the output list
}

Long64_t NGnThreadData::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///
  Long64_t nmerged = 0;

  NLogger::Trace("NGnThreadData::Merge: BEGIN ------------------------------------------------");
  NLogger::Trace("NGnThreadData::Merge: Merging thread data from %zu threads ...", list->GetEntries());

  NStorageTree *                 ts = nullptr;
  std::map<std::string, TList *> listOutputs;

  TList * listOut         = new TList();
  TList * listTreeStorage = new TList();

  for (auto obj : *list) {
    if (obj->IsA() == NGnThreadData::Class()) {
      NGnThreadData * hnsttd = (NGnThreadData *)obj;
      NLogger::Debug("NGnThreadData::Merge: Merging thread %zu processed %lld ...", hnsttd->GetAssignedIndex(),
                     hnsttd->GetNProcessed());
      ts = hnsttd->GetHnSparseBase()->GetStorageTree();
      if (!ts) {
        NLogger::Error("NGnThreadData::Merge: Storage tree is not set in NGnTree !!!");
        continue;
      }
      // hnsttd->Print();

      for (auto & kv : hnsttd->GetHnSparseBase()->GetOutputs()) {
        NLogger::Trace("NGnThreadData::Merge: Found output list '%s' with %d objects", kv.first.c_str(),
                       kv.second ? kv.second->GetEntries() : 0);
        if (kv.second && !kv.second->IsEmpty()) {
          NLogger::Trace("NGnThreadData::Merge: Merging output list '%s' with %d objects", kv.first.c_str(),
                         kv.second->GetEntries());
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

      NGnTree * hnsb = NGnTree::Open(ts->GetFileName());
      if (!hnsb) {
        NLogger::Error("NGnThreadData::Merge: Failed to open NGnTree from file '%s' !!!",
                       ts->GetFileName().c_str());
        continue;
      }

      // hnsb->Print();
      listTreeStorage->Add(hnsb->GetStorageTree());

      // TODO: check if needed
      // fHnSparseBase->GetBinning()->GetDefinition()->GetContent()->Add(
      //     hnsb->GetBinning()->GetDefinition()->GetContent());
      nmerged++;
    }
  }
  // FIXME: Fix this properly [it should be ok now]
  fHnSparseBase->GetBinning()->GetContent()->Reset();
  // Print hnsb binning definition ids
  for (const auto & name : fBiningSource->GetDefinitionNames()) {
    NBinningDef * targetBinningDef = fBiningSource->GetDefinition(name);
    for (auto id : targetBinningDef->GetIds()) {
      NBinningPoint point(fHnSparseBase->GetBinning());
      fBiningSource->GetContent()->GetBinContent(id, point.GetCoords());
      Long64_t bin = fHnSparseBase->GetBinning()->GetContent()->GetBin(point.GetCoords());
      NLogger::Trace("NGnThreadData::Merge: Adding def_id=%lld to content_bin=%lld", id, bin);
      fHnSparseBase->GetBinning()->GetContent()->SetBinContent(bin, id);

      // fHnSparseBase->GetBinning()->GetDefinition(name)->GetContent()->SetBinContent(bin, id);
      fHnSparseBase->GetBinning()->GetDefinition(name)->GetIds().push_back(id);
    }
    // fHnSparseBase->GetBinning()->GetDefinition(name)->Print();
  }
  // FIXME: End
  NLogger::Debug("NGnThreadData::Merge: Total entries to merge: %lld", nmerged);

  for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
    auto binningDef = fHnSparseBase->GetBinning()->GetDefinition(name);
    if (!binningDef) {
      NLogger::Error("NGnThreadData::Merge: Binning definition '%s' not found in NGnTree !!!",
                     name.c_str());
      continue;
    }
    // add ids from fBiningSource to binningDef
    // binningDef->GetIds().insert(binningDef->GetIds().end(), fBiningSource->GetDefinition(name)->GetIds().begin(),
    //                             fBiningSource->GetDefinition(name)->GetIds().end());
    NLogger::Trace("NGnThreadData::Merge: Final IDs in definition '%s': %s", name.c_str(),
                   NUtils::GetCoordsString(binningDef->GetIds(), -1).c_str());
  }

  // fHnSparseBase->GetBinning()->GetContent()->Projection(5)->Print("all");
  // NLogger::Debug("Not ready to merge, exiting ...");
  // return 0;

  NLogger::Trace("NGnThreadData::Merge: Merging %d storage trees ...", listTreeStorage->GetEntries());
  // fHnSparseBase->GetBinning()->GetPoint()->Reset();
  // fHnSparseBase->GetBinning()->GetDefinition("default")->Print();
  // fHnSparseBase->GetBinning()->GetDefinition("b2")->Print();
  fHnSparseBase->GetStorageTree()->SetBinning(fHnSparseBase->GetBinning()); // Update binning to the merged one
  fHnSparseBase->GetStorageTree()->Merge(listTreeStorage);
  // NLogger::Debug("Not ready to merge after Storage merge, exiting ...");
  // return 0;

  // loop over all output lists and merge them
  for (auto & kv : listOutputs) {
    if (kv.second && !kv.second->IsEmpty()) {
      NLogger::Trace("NGnThreadData::Merge: Merging output list '%s' with %d objects", kv.first.c_str(),
                     kv.second->GetEntries() + 1);
      fHnSparseBase->GetOutput(kv.first)->Merge(kv.second);
      // fHnSparseBase->GetOutput(kv.first)->Print();
    }
  }

  // Loop over binning definitions and merge their contents
  fHnSparseBase->GetStorageTree()->SetEnabledBranches({}, 0);
  for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
    NBinningDef * binningDef = fHnSparseBase->GetBinning()->GetDefinition(name);
    if (!binningDef) {
      NLogger::Error("NGnThreadData::Merge: Binning definition '%s' not found in NGnTree !!!",
                     name.c_str());
      continue;
    }
    // binningDef->Print();
    // Recalculate binningDef content based on ids
    binningDef->GetContent()->Reset();
    for (auto id : binningDef->GetIds()) {
      fHnSparseBase->GetEntry(id, false);
      Long64_t bin = binningDef->GetContent()->GetBin(fHnSparseBase->GetBinning()->GetPoint()->GetStorageCoords());
      binningDef->GetContent()->SetBinContent(bin, id);
      // binningDef->GetIds().push_back(id);
      NLogger::Trace("NGnThreadData::Merge: -> Setting content bin %lld to id %lld", bin, id);
    }
  }

  // print all definitions
  // for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
  //   fHnSparseBase->GetBinning()->GetDefinition(name)->Print();
  // }

  // Set default setting
  fHnSparseBase->GetBinning()->GetPoint()->Reset();
  fHnSparseBase->GetStorageTree()->SetEnabledBranches({}, 1);
  fHnSparseBase->GetBinning()->SetCurrentDefinitionName(fHnSparseBase->GetBinning()->GetDefinitionNames().front());

  NLogger::Trace("NGnThreadData::Merge: END ------------------------------------------------");

  /// \cond CLASSIMP
  // NLogger::Error("NGnThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}
} // namespace Ndmspc
