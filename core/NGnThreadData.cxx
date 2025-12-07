#include <NGnTree.h>
#include <NStorageTree.h>
#include <TList.h>
#include <TThread.h>
#include "THnSparse.h"
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NGnThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnThreadData);
/// \endcond

namespace Ndmspc {
NGnThreadData::NGnThreadData() : NThreadData() {}
NGnThreadData::~NGnThreadData() {}
bool NGnThreadData::Init(size_t id, NHnSparseProcessFuncPtr func, NGnTree * ngnt, NBinning * binningIn, NGnTree * input,
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
  //   NLogError("NGnThreadData::Init: Process function is not set !!!");
  //   return false;
  // }
  fProcessFunc = func;

  if (ngnt == nullptr) {
    NLogError("NGnThreadData::Init: NGnTree is nullptr !!!");
    return false;
  }
  fBiningSource = binningIn;

  if (fBiningSource == nullptr) {
    NLogError("NGnThreadData::Init: Binning Source is nullptr !!!");
    return false;
  }

  fHnSparseBase = (NGnTree *)ngnt->Clone();
  // fHnSparseBase = new NGnTree(hnsb->GetBinning(), (NStorageTree *)hnsb->GetStorageTree()->Clone());
  // fHnSparseBase = new NGnTree(hnsb->GetBinning(), new NStorageTree(hnsb->GetBinning()));
  // fHnSparseBase = new NGnTree(hnsb->GetBinning(), nullptr);

  if (fHnSparseBase->GetBinning() == nullptr) {
    NLogError("NGnThreadData::InitStorage: Binning is not set !!!");
    return false;
  }

  if (fHnSparseBase->GetStorageTree() == nullptr) {
    NLogError("NGnThreadData::InitStorage: Storage tree is not set !!!");
    return false;
  }

  NStorageTree * ts = fHnSparseBase->GetStorageTree();
  std::string    fn = ts->GetFileName();
  fHnSparseBase->GetStorageTree()->Clear("F");
  fHnSparseBase->GetStorageTree()->InitTree(filename.empty() ? fn : filename, treename);

  // loop over all branches and add them to the new storage tree
  for (auto & kv : ngnt->GetStorageTree()->GetBranchesMap()) {
    NLogTrace("NGnThreadData::Init: Adding branch '%s' to thread %zu", kv.first.c_str(), id);
    NTreeBranch * b = fHnSparseBase->GetStorageTree()->GetBranch(kv.first);
    if (b) continue;

    b = fHnSparseBase->GetStorageTree()->GetBranch(kv.first);
    if (b) continue;

    fHnSparseBase->GetStorageTree()->AddBranch(kv.first, nullptr, kv.second.GetObjectClassName());
  }

  // Recreate the point and set the storage tree
  fHnSparseBase->GetBinning()->GetPoint()->SetTreeStorage(fHnSparseBase->GetStorageTree());

  // for (auto & kv : ts->GetBranchesMap()) {
  //   NLogTrace("NGnThreadData::Init: Adding branch '%s' to thread %zu", kv.first.c_str(), id);
  //   fHnSparseBase->GetStorageTree()->AddBranch(kv.first, nullptr, kv.second.GetObjectClassName());
  // }

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

  if (input) {
    std::string branches = NUtils::Join(input->GetStorageTree()->GetBrancheNames(true), ',');
    fHnSparseBase->SetInput(NGnTree::Open(input->GetStorageTree()->GetFileName(), branches)); // Set the input NGnTree
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
  TH1::AddDirectory(kFALSE); // Disable ROOT auto directory management

  fNProcessed++;
  // NThreadData::Process(coords);

  // NLogDebug("NGnThreadData::Process: Thread %d processing coordinates %s", GetAssignedIndex(),
  //                NUtils::GetCoordsString(coords).c_str());

  if (!fHnSparseBase) {
    NLogError("NGnThreadData::Process: NGnTree is not set in NGnThreadData !!!");
    return;
  }

  if (!fProcessFunc) {
    NLogError("NGnThreadData::Process: Process function is not set in NGnThreadData !!!");
    return;
  }

  // NBinning *     binning    = fBiningSource;
  NBinningDef * binningDef = fBiningSource->GetDefinition();
  if (binningDef == nullptr) {
    NLogError("NGnThreadData::Process: Binning definition is not set in NGnThreadData !!!");
    return;
  }

  NStorageTree * ts = fHnSparseBase->GetStorageTree();
  NGnTree *      in = fHnSparseBase->GetInput();

  NBinningPoint * point = fHnSparseBase->GetBinning()->GetPoint();

  Long64_t entry = fBiningSource->GetDefinition()->GetId(coords[0]);

  if (entry < fHnSparseBase->GetBinning()->GetContent()->GetNbins()) {
    fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(entry);
    // entry = binningDef->GetContent()->GetBinContent(entry);
    // point->SetEntryNumber(entry);
    NLogDebug("NGnThreadData::Process: [%zu] Skipping entry=%lld, because it was already process !!!",
                   GetAssignedIndex(), entry);
    return;
  }

  if (fResourceMonitor == nullptr) {
    fResourceMonitor = new NResourceMonitor();
    fResourceMonitor->Initialize(binningDef->GetContent());
    fHnSparseBase->GetOutput()->Add(fResourceMonitor->GetHnSparse());
  }

  // Long64_t        entry = binningDef->GetId(coords[0]);
  // NLogDebug("NGnThreadData::Process: [%zu] Entry in global content mapping: %lld",
  //                        GetAssignedIndex(), entry);
  fBiningSource->GetContent()->GetBinContent(entry, point->GetCoords());
  point->RecalculateStorageCoords(entry, false);
  point->SetCfg(fCfg); // Set configuration to the point
                       // point->Print("C");

  // TODO: check if entry was already processed
  // So we dont execute the function again

  // NLogDebug(
  //     "AAA NGnThreadData::Process: Thread %zu processing entry %lld for coordinates %s", GetAssignedIndex(),
  //     entry,
  //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
  point->SetTreeStorage(ts); // Set the storage tree to the binning point
  point->SetInput(in);       // Set the input NGnTree to the binning point
  TList * outputPoint = new TList();

  fResourceMonitor->Start();

  fProcessFunc(point, fHnSparseBase->GetOutput(), outputPoint, GetAssignedIndex());

  fResourceMonitor->End();
  fResourceMonitor->Fill(point->GetStorageCoords(), GetAssignedIndex());

  // NLogTrace(
  //     "NGnThreadData::Process: [%zu] entry=%lld coords=%s outputPoint=%d", GetAssignedIndex(), entry,
  //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str(),
  //     outputPoint->GetEntries());
  if (outputPoint->GetEntries() > 0) {
    NLogTrace(
        "NGnThreadData::Process: [%zu] Entry '%lld' was accepted. %s", GetAssignedIndex(), entry,
        NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());

    ts->GetBranch("outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
    // ts->Fill(point, nullptr, false, {}, false);
    Int_t bytes = ts->Fill(point, nullptr, false, {}, false);
    if (bytes > 0) {
      // Long64_t entryInBinDef = binningDefgcc->GetId(coords[0]);
      // NLogDebug("NGnThreadData::Process: Thread %zu: Filled %d bytes for coordinates %s entry=%lld",
      //                GetAssignedIndex(), bytes, NUtils::GetCoordsString(coords).c_str(), entry);

      fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(entry);
      // NLogInfo("Entry number in storage tree: %lld", point->GetEntryNumber());
      // fHnSparseBase->GetBinning()->GetDefinition()->GetIds().push_back(point->GetEntryNumber());
    }
    else {
      NLogTrace("NGnThreadData::Process: [%zu] Entry '%lld' Fill was done with 0 bytes. Skipping ...",
                             GetAssignedIndex(), entry);
      // NLogError("NGnThreadData::Process: Thread %zu: zero bytes were writtent for coordinates %s
      // entry=%lld",
      //                GetAssignedIndex(), NUtils::GetCoordsString(coords).c_str(), entry);
    }
    // outputPoint->Print();
    // outputPoint->Clear(); // Clear the list to avoid memory leaks
  }
  else {
    NLogTrace(
        "NGnThreadData::Process: [%zu] Entry '%lld' No output %s. Skipping ...", GetAssignedIndex(), entry,
        NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
    // NLogTrace(
    //     "No output for coordinates %s",
    //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());
  }

  // protect this section with mutex if needed

  {
    std::lock_guard<std::mutex> lock(fSharedMutex);
    // TThread::Lock();

    for (Int_t i = 0; i < outputPoint->GetEntries(); ++i) {
      TObject * obj = outputPoint->At(i);
      if (obj) {
        // obj->SetDirectory(nullptr); // Detach from any directory to avoid memory leaks
        outputPoint->Remove(obj); // Remove if already exists
        delete obj;               // Delete the object to avoid memory leaks
      }
    }
    outputPoint->Clear();
    delete outputPoint; // Clean up the output list
    // TThread::UnLock();
  }
}

Long64_t NGnThreadData::Merge(TCollection * list)
{
  ///
  /// Merge function
  ///
  Long64_t nmerged = 0;

  NLogTrace("NGnThreadData::Merge: BEGIN ------------------------------------------------");
  NLogTrace("NGnThreadData::Merge: Merging thread data from %zu threads ...", list->GetEntries());

  NStorageTree *                 ts = nullptr;
  std::map<std::string, TList *> listOutputs;

  TList * listOut         = new TList();
  TList * listTreeStorage = new TList();

  for (auto obj : *list) {
    if (obj->IsA() == NGnThreadData::Class()) {
      NGnThreadData * hnsttd = (NGnThreadData *)obj;
      NLogDebug("NGnThreadData::Merge: Merging thread %zu processed %lld ...", hnsttd->GetAssignedIndex(),
                     hnsttd->GetNProcessed());
      ts = hnsttd->GetHnSparseBase()->GetStorageTree();
      if (!ts) {
        NLogError("NGnThreadData::Merge: Storage tree is not set in NGnTree !!!");
        continue;
      }
      // hnsttd->Print();

      for (auto & kv : hnsttd->GetHnSparseBase()->GetOutputs()) {
        NLogTrace("NGnThreadData::Merge: Found output list '%s' with %d objects", kv.first.c_str(),
                       kv.second ? kv.second->GetEntries() : 0);
        if (kv.second && !kv.second->IsEmpty()) {
          NLogTrace("NGnThreadData::Merge: Merging output list '%s' with %d objects", kv.first.c_str(),
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
        NLogError("NGnThreadData::Merge: Failed to open NGnTree from file '%s' !!!", ts->GetFileName().c_str());
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
      NLogTrace("NGnThreadData::Merge: Adding def_id=%lld to content_bin=%lld", id, bin);
      fHnSparseBase->GetBinning()->GetContent()->SetBinContent(bin, id);

      // fHnSparseBase->GetBinning()->GetDefinition(name)->GetContent()->SetBinContent(bin, id);
      fHnSparseBase->GetBinning()->GetDefinition(name)->GetIds().push_back(id);
    }
    // fHnSparseBase->GetBinning()->GetDefinition(name)->Print();
  }
  // FIXME: End
  NLogDebug("NGnThreadData::Merge: Total entries to merge: %lld", nmerged);

  for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
    auto binningDef = fHnSparseBase->GetBinning()->GetDefinition(name);
    if (!binningDef) {
      NLogError("NGnThreadData::Merge: Binning definition '%s' not found in NGnTree !!!", name.c_str());
      continue;
    }
    // add ids from fBiningSource to binningDef
    // binningDef->GetIds().insert(binningDef->GetIds().end(), fBiningSource->GetDefinition(name)->GetIds().begin(),
    //                             fBiningSource->GetDefinition(name)->GetIds().end());
    NLogTrace("NGnThreadData::Merge: Final IDs in definition '%s': %s", name.c_str(),
                   NUtils::GetCoordsString(binningDef->GetIds(), -1).c_str());
  }

  // fHnSparseBase->GetBinning()->GetContent()->Projection(5)->Print("all");
  // NLogDebug("Not ready to merge, exiting ...");
  // return 0;

  NLogTrace("NGnThreadData::Merge: Merging %d storage trees ...", listTreeStorage->GetEntries());
  // fHnSparseBase->GetBinning()->GetPoint()->Reset();
  // fHnSparseBase->GetBinning()->GetDefinition("default")->Print();
  // fHnSparseBase->GetBinning()->GetDefinition("b2")->Print();
  fHnSparseBase->GetStorageTree()->SetBinning(fHnSparseBase->GetBinning()); // Update binning to the merged one
  fHnSparseBase->GetStorageTree()->Merge(listTreeStorage);
  // NLogDebug("Not ready to merge after Storage merge, exiting ...");
  // return 0;

  // loop over all output lists and merge them
  for (auto & kv : listOutputs) {
    if (kv.second && !kv.second->IsEmpty()) {
      NLogTrace("NGnThreadData::Merge: Merging output list '%s' with %d objects", kv.first.c_str(),
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
      NLogError("NGnThreadData::Merge: Binning definition '%s' not found in NGnTree !!!", name.c_str());
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
      NLogTrace("NGnThreadData::Merge: -> Setting content bin %lld to id %lld", bin, id);
    }
  }

  // print all definitions
  // for (const auto & name : fHnSparseBase->GetBinning()->GetDefinitionNames()) {
  //   fHnSparseBase->GetBinning()->GetDefinition(name)->Print();
  // }

  if (fHnSparseBase->GetInput()) {
    fHnSparseBase->GetInput()->Close(false);
  }

  // Set default setting
  fHnSparseBase->GetBinning()->GetPoint()->Reset();
  fHnSparseBase->GetStorageTree()->SetEnabledBranches({}, 1);
  fHnSparseBase->GetBinning()->SetCurrentDefinitionName(fHnSparseBase->GetBinning()->GetDefinitionNames().front());

  NLogTrace("NGnThreadData::Merge: END ------------------------------------------------");

  /// \cond CLASSIMP
  // NLogError("NGnThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}
} // namespace Ndmspc
