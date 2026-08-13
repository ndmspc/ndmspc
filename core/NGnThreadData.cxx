
#include <NStorageTree.h>
#include <TROOT.h>
#include <TCanvas.h>
#include <TSystem.h>
#include <mutex>
#include <THnSparse.h>
#include "NBinningPoint.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NGnThreadData.h"
#include "NGnTree.h"
/// \cond CLASSIMP
ClassImp(Ndmspc::NGnThreadData);
/// \endcond

namespace Ndmspc {
NGnThreadData::NGnThreadData() : NThreadData() {}
NGnThreadData::~NGnThreadData() {}
bool NGnThreadData::Init(size_t id, NGnProcessFuncPtr func, NGnBeginFuncPtr funcBegin, NGnEndFuncPtr endFunc,
                         NGnTree * ngnt, NBinning * binningIn, NGnTree * input, const std::string & filename,
                         const std::string & treename)
{
  ///
  /// Initialize thread data
  ///
  SetAssignedIndex(id);
  SetThreadId(std::this_thread::get_id());

  TH1::AddDirectory(kFALSE); // Disable ROOT auto directory management

  // if (!func) {
  //   NLogError("NGnThreadData::Init: Process function is not set !!!");
  //   return false;
  // }
  fBeginFunc   = funcBegin;
  fProcessFunc = func;
  fEndFunc     = endFunc;

  if (ngnt == nullptr) {
    NLogError("NGnThreadData::Init: NGnTree is nullptr !!!");
    return false;
  }

  fIsPureCopy = ngnt->IsPureCopy();

  fBinningSource = binningIn;

  if (fBinningSource == nullptr) {
    NLogError("NGnThreadData::Init: Binning Source is nullptr !!!");
    return false;
  }

  fHnSparseBase = (NGnTree *)ngnt->Clone();

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
  ts->Clear("F");
  ts->InitTree(filename.empty() ? fn : filename, treename);

  NTreeBranch * b = nullptr;
  // loop over all branches and add them to the new storage tree
  for (auto & kv : ngnt->GetStorageTree()->GetBranchesMap()) {
    NLogTrace("NGnThreadData::Init: Adding branch '%s' to thread %zu", kv.first.c_str(), id);
    b = ts->GetBranch(kv.first);
    if (b) continue;

    b = ts->GetBranch(kv.first);
    if (b) continue;

    ts->AddBranch(kv.first, nullptr, kv.second.GetObjectClassName());
  }
  b = ts->GetBranch("_outputPoint");
  if (!b) ts->AddBranch("_outputPoint", nullptr, "TList");

  if (ngnt->GetParameters()) {
    NLogTrace("NGnThreadData::Init: Setting parameters branch for thread %zu", id);
    b = ts->GetBranch("_params");
    if (!b) ts->AddBranch("_params", nullptr, "Ndmspc::NParameters");

    NParameters * params = (NParameters *)ngnt->GetParameters()->Clone();
    ts->GetBranch("_params")->SetAddress(params);
    fHnSparseBase->GetBinning()->GetPoint()->SetParameters(params);
  }

  // Recreate the point and set the storage tree
  fHnSparseBase->GetBinning()->GetPoint()->SetTreeStorage(fHnSparseBase->GetStorageTree());

  for (auto & kv : fHnSparseBase->GetBinning()->GetDefinitions()) {
    NBinningDef * def = kv.second;
    if (def) {
      def->GetContent()->Reset();
      def->GetIds().clear();
    }
  }

  if (input) {
    NLogTrace("NGnThreadData::Init: Setting input NGnTree for thread %zu '%s'", id,
              input->GetStorageTree()->GetFileName().c_str());
    std::string branches = NUtils::Join(input->GetStorageTree()->GetBrancheNames(true), ',');
    fHnSparseBase->SetInput(NGnTree::Open(input->GetStorageTree()->GetFileName(), branches)); // Set the input NGnTree
  }

  ExecuteBeginFunction();

  return true;
}

void NGnThreadData::Process(const std::vector<int> & coords)
{
  /// Process method
  /// This method is called for each set of coordinates
  /// It initializes the NHnSparseTree if not already done
  TH1::AddDirectory(kFALSE); // Disable ROOT auto directory management

  // Ensure this thread has a current pad so that user code calling h->Fit()
  // does not trigger the non-thread-safe TCanvas::MakeDefCanvas().
  // gPad is thread_local in ROOT 6: each worker thread starts with nullptr.
  // We create a minimal batch canvas (batch mode is already set by
  // NGnTree::Process before ExecuteParallel) and serialise the one-time
  // TCanvas::Constructor call with a static mutex.  After this block gPad is
  // set for this thread and subsequent Fit() calls find it non-null.
  if (!gPad) {
    static std::mutex           sPadMutex;
    std::lock_guard<std::mutex> lk(sPadMutex);
    // gPad is still nullptr for THIS thread even inside the lock (thread-local);
    // the mutex only serialises concurrent TCanvas::Constructor calls.
    TString cname = TString::Format("_ndmspc_wk%zu", GetAssignedIndex());
    auto *  c     = new TCanvas(cname, cname, 1, 1);
    fDeferredDeletes.push_back(c); // cleaned up by FlushDeferredDeletes
  }

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

  // NBinning *     binning    = fBinningSource;
  NBinningDef * binningDef = fBinningSource->GetDefinition();
  if (binningDef == nullptr) {
    NLogError("NGnThreadData::Process: Binning definition is not set in NGnThreadData !!!");
    return;
  }

  NStorageTree * ts = fHnSparseBase->GetStorageTree();
  NGnTree *      in = fHnSparseBase->GetInput();

  NBinningPoint * point = fHnSparseBase->GetBinning()->GetPoint();

  Long64_t entry = -1;
  if (!fCurrentDefinitionIds.empty()) {
    if (coords.empty() || coords[0] < 0 || static_cast<size_t>(coords[0]) >= fCurrentDefinitionIds.size()) {
      NLogError("NGnThreadData::Process: Invalid task coordinate index=%d for worker-local ids size=%zu",
                coords.empty() ? -1 : coords[0], fCurrentDefinitionIds.size());
      return;
    }
    entry = fCurrentDefinitionIds[coords[0]];
  }
  else {
    entry = fBinningSource->GetDefinition()->GetId(coords[0]);
  }

  if (fProcessedBinIds.count(entry)) {
    NLogDebug("NGnThreadData::Process: [%zu] Skipping entry=%lld, because it was already process !!!",
              GetAssignedIndex(), entry);
    return;
  }
  fProcessedBinIds.insert(entry);

  if (fResourceMonitor == nullptr) {
    int monitorWorkers = 0;
    if (fCfg.contains("_ndmspc") && fCfg["_ndmspc"].is_object() && fCfg["_ndmspc"].contains("workerCount") &&
        fCfg["_ndmspc"]["workerCount"].is_number_integer()) {
      monitorWorkers = fCfg["_ndmspc"]["workerCount"].get<int>();
    }
    // Fallback: if workerCount not provided via cfg, check environment variable set during bootstrap
    if (monitorWorkers == 0) {
      const char * env = gSystem->Getenv("NDMSPC_MAX_PROCESSES");
      if (env && env[0] != '\0') {
        try {
          int parsed = std::stoi(std::string(env));
          if (parsed > 0) monitorWorkers = parsed;
        }
        catch (...) {
          // ignore parse errors and leave monitorWorkers as 0
        }
      }
    }
    fResourceMonitor = new NResourceMonitor();
    fResourceMonitor->Initialize(binningDef->GetContent(), monitorWorkers);
    fHnSparseBase->GetOutput()->Add(fResourceMonitor->GetHnSparse());
  }

  // Long64_t        entry = binningDef->GetId(coords[0]);
  // NLogDebug("NGnThreadData::Process: [%zu] Entry in global content mapping: %lld",
  //                        GetAssignedIndex(), entry);
  fBinningSource->GetContent()->GetBinContent(entry, point->GetCoords());
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

  if (!point->GetCfg()["_ndmspc"].is_null()) {
    fCfg["_ndmspc"] = point->GetCfg()["_ndmspc"]; // Get configuration from the point
  }

  // NLogTrace(
  //     "NGnThreadData::Process: [%zu] entry=%lld coords=%s outputPoint=%d", GetAssignedIndex(), entry,
  //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str(),
  //     outputPoint->GetEntries());
  if (outputPoint->GetEntries() > 0) {
    NLogTrace(
        "NGnThreadData::Process: [%zu] Entry '%lld' was accepted. %s", GetAssignedIndex(), entry,
        NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(), point->GetNDimensionsContent())).c_str());

    if (!fIsPureCopy) {
      ts->GetBranch("_outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
    }
    //
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
    //     NUtils::GetCoordsString(NUtils::ArrayToVector(point->GetCoords(),
    //     point->GetNDimensionsContent())).c_str());
  }

  // Defer deletion to FlushDeferredDeletes() on the main thread.
  // ROOT's cleanup machinery (GarbageCollect, RecursiveRemove) is not thread-safe.
  {
    NLogTrace("NGnThreadData::Process: [%zu] Cleaning output list with %d entries for entry '%lld' ...",
              GetAssignedIndex(), outputPoint->GetEntries(), entry);
    if (!fIsPureCopy) {
      TObject * obj = nullptr;
      while ((obj = outputPoint->First())) {
        outputPoint->Remove(obj);
        fDeferredDeletes.push_back(obj);
      }
      delete outputPoint;
      FlushDeferredDeletes();
    }
  }
}

void NGnThreadData::SetCurrentDefinitionName(const std::string & name)
{
  fProcessedBinIds.clear();
  fCurrentDefinitionIds.clear();
  if (fHnSparseBase && fHnSparseBase->GetBinning()) {
    fHnSparseBase->GetBinning()->SetCurrentDefinitionName(name);
  }
  if (fBinningSource) {
    fBinningSource->SetCurrentDefinitionName(name);
  }
}

void NGnThreadData::SyncCurrentDefinitionIds(const std::vector<Long64_t> & ids)
{
  // fHnSparseBase tracks only the entries actually written by this worker.
  // Clear it so Process() builds the list from scratch (same as thread mode via Init).
  // Do NOT assign the full 'ids' list here — that would include unprocessed entries
  // (e.g., even-indexed entries when onlyOddPoints=true) and corrupt the merge step.
  if (fHnSparseBase && fHnSparseBase->GetBinning()) {
    if (auto * def = fHnSparseBase->GetBinning()->GetDefinition()) {
      def->GetIds().clear();
    }
  }
  // Keep per-worker lookup IDs local; do not mutate shared source binning.
  fCurrentDefinitionIds = ids;
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

  auto queueOutputListForMerge = [this, &listOutputs](const std::string & key, TList * src) {
    if (!src || src->IsEmpty()) return;

    NLogTrace("NGnThreadData::Merge: Queuing output list '%s' with %d objects", key.c_str(), src->GetEntries());
    if (listOutputs.find(key) == listOutputs.end()) {
      listOutputs[key] = new TList();
    }

    if (fHnSparseBase->GetOutput(key)->IsEmpty()) {
      // Seed merged output once; remaining contributor lists are merged below.
      fHnSparseBase->GetOutput(key)->AddAll(src);
    }
    else {
      listOutputs[key]->Add(src);
    }
  };

  // TList * listOut         = new TList();
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
        NLogTrace("NGnThreadData::Merge: Found in-memory output list '%s' with %d objects", kv.first.c_str(),
                  kv.second ? kv.second->GetEntries() : 0);
        queueOutputListForMerge(kv.first, kv.second);
      }

      // if (fOutput == nullptr) {
      //   fOutput = hnsttd->GetOutput();
      // }
      // else {
      //   listOut->Add(hnsttd->GetOutput());
      // }

      const std::string mergeFilename =
          hnsttd->GetResultsFilename().empty() ? ts->GetFileName() : hnsttd->GetResultsFilename();
      NGnTree * ngntmerge = NGnTree::Open(mergeFilename);
      if (!ngntmerge) {
        NLogError("NGnThreadData::Merge: Failed to open NGnTree from file '%s' !!!", mergeFilename.c_str());
        continue;
      }

      // In IPC/process mode worker-side output lists live in the worker files,
      // not in parent in-memory worker objects. Merge these lists explicitly.
      for (auto & kv : ngntmerge->GetOutputs()) {
        NLogTrace("NGnThreadData::Merge: Found file output list '%s' with %d objects from '%s'", kv.first.c_str(),
                  kv.second ? kv.second->GetEntries() : 0, mergeFilename.c_str());
        queueOutputListForMerge(kv.first, kv.second);
      }

      listTreeStorage->Add(ngntmerge->GetStorageTree());
      nmerged++;
    }
  }

  Long64_t      bin;
  NBinningPoint point(fHnSparseBase->GetBinning());

  fHnSparseBase->GetBinning()->GetContent()->Reset();
  for (Long64_t i = 0; i < fBinningSource->GetContent()->GetNbins(); ++i) {
    fBinningSource->GetContent()->GetBinContent(i, point.GetCoords());
    bin = fHnSparseBase->GetBinning()->GetContent()->GetBin(point.GetCoords());
    NLogTrace("NGnThreadData::Merge: Adding bin=%lld to content_bin=%lld", bin, i);
    fHnSparseBase->GetBinning()->GetContent()->SetBinContent(bin, i);
  }

  NLogDebug("NGnThreadData::Merge: Total entries to merge: %lld", nmerged);
  NLogTrace("NGnThreadData::Merge: Merging %d storage trees ...", listTreeStorage->GetEntries());
  fHnSparseBase->GetStorageTree()->SetBinning(fHnSparseBase->GetBinning()); // Update binning to the merged one
  fHnSparseBase->GetStorageTree()->Merge(listTreeStorage);

  // loop over all output lists and merge them
  for (auto & kv : listOutputs) {
    // Diagnostics: print object names already present in the seeded target list.
    {
      std::vector<std::string> targetNames;
      TList *                  targetOut = fHnSparseBase->GetOutput(kv.first);
      if (targetOut) {
        TObject * o = nullptr;
        TIter     nextTarget(targetOut);
        while ((o = nextTarget())) {
          targetNames.push_back(o->GetName() ? o->GetName() : "");
        }
      }
      NLogDebug("NGnThreadData::Merge: Output '%s' target has %zu object(s) before merge: %s", kv.first.c_str(),
                targetNames.size(), NUtils::GetCoordsString(targetNames).c_str());
    }

    // Diagnostics: summarize every contributor list for this binning.
    {
      std::vector<std::string> contributorSummary;
      if (kv.second) {
        TIter nextList(kv.second);
        while (auto * obj = nextList()) {
          TList * src = dynamic_cast<TList *>(obj);
          if (!src) continue;
          std::vector<std::string> srcNames;
          TIter                    nextObj(src);
          while (auto * so = nextObj()) {
            srcNames.push_back(so->GetName() ? so->GetName() : "");
          }
          contributorSummary.push_back(
              TString::Format("[%d]%s", src->GetEntries(), NUtils::GetCoordsString(srcNames).c_str()).Data());
        }
      }
      NLogDebug("NGnThreadData::Merge: Output '%s' has %zu contributor list(s): %s", kv.first.c_str(),
                contributorSummary.size(), NUtils::GetCoordsString(contributorSummary).c_str());
    }

    if (kv.second && !kv.second->IsEmpty()) {
      NLogTrace("NGnThreadData::Merge: Merging output list '%s' with %d objects", kv.first.c_str(),
                kv.second->GetEntries() + 1);
      fHnSparseBase->GetOutput(kv.first)->Merge(kv.second);
      // fHnSparseBase->GetOutput(kv.first)->Print();
    }

    // Diagnostics: print final merged object names per binning.
    {
      std::vector<std::string> mergedNames;
      TList *                  mergedOut = fHnSparseBase->GetOutput(kv.first);
      if (mergedOut) {
        TObject * o = nullptr;
        TIter     nextMerged(mergedOut);
        while ((o = nextMerged())) {
          mergedNames.push_back(o->GetName() ? o->GetName() : "");
        }
      }
      NLogInfo("NGnThreadData::Merge: Output '%s' merged to %zu object(s): %s", kv.first.c_str(), mergedNames.size(),
               NUtils::GetCoordsString(mergedNames).c_str());
    }
  }
  // print all definitions
  for (auto & def : fHnSparseBase->GetBinning()->GetDefinitions()) {
    def.second->Print();
  }

  // Set default setting
  fHnSparseBase->GetBinning()->GetPoint()->Reset();
  fHnSparseBase->GetBinning()->SetCurrentDefinitionName(fHnSparseBase->GetBinning()->GetDefinitionNames().front());
  fHnSparseBase->GetBinning()->Print();
  NLogTrace("NGnThreadData::Merge: END ------------------------------------------------");

  /// \cond CLASSIMP
  // NLogError("NGnThreadData::Merge: Not implemented !!!");
  /// \endcond;
  return nmerged;
}

void NGnThreadData::ExecuteBeginFunction()
{
  if (fBeginFunc) {
    fBeginFunc(fHnSparseBase->GetBinning()->GetPoint(), GetAssignedIndex());
  }
}

void NGnThreadData::ExecuteEndFunction()
{
  if (fEndFunc) {
    fEndFunc(fHnSparseBase->GetBinning()->GetPoint(), GetAssignedIndex());
  }
}

void NGnThreadData::FlushDeferredDeletes()
{
  if (fDeferredDeletes.empty()) return;

  NLogTrace("NGnThreadData::FlushDeferredDeletes: [%zu] Deleting %zu deferred objects ...", GetAssignedIndex(),
            fDeferredDeletes.size());

  NUtils::SafeDeleteObjects(fDeferredDeletes);
}

} // namespace Ndmspc
