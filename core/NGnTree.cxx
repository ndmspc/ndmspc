#include <cstddef>
#include <string>
#include <vector>
#include <iostream>
#include "TDirectory.h"
#include "TObject.h"
#include <TList.h>
#include <TROOT.h>
#include <THnSparse.h>
#include <TH1.h>
#include <TCanvas.h>
#include <TSystem.h>
#include <TMap.h>
#include <TObjString.h>
#include <TTree.h>
#include <TBufferJSON.h>
#include "NStorageTree.h"
#include "NBinning.h"
#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NGnThreadData.h"
#include "NLogger.h"
#include "NTreeBranch.h"
#include "NUtils.h"
#include "NStorageTree.h"
#include "NWsClient.h"
#include "NGnTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnTree);
/// \endcond

namespace Ndmspc {
NGnTree::NGnTree() : TObject() {}
NGnTree::NGnTree(std::vector<TAxis *> axes, std::string filename, std::string treename) : TObject()
{
  ///
  /// Constructor
  ///
  if (axes.empty()) {
    NLogger::Error("NGnTree::NGnTree: No axes provided, binning is nullptr.");
    // fBinning = new NBinning();
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
}
NGnTree::NGnTree(TObjArray * axes, std::string filename, std::string treename) : TObject()
{
  ///
  /// Constructor
  ///
  if (axes == nullptr && axes->GetEntries() == 0) {
    NLogger::Error("NGnTree::NGnTree: No axes provided, binning is nullptr.");
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
}

NGnTree::NGnTree(NGnTree * ngnt, std::string filename, std::string treename) : TObject()
{
  ///
  /// Constructor
  ///
  if (ngnt == nullptr) {
    NLogger::Error("NGnTree::NGnTree: NGnTree is nullptr.");
    MakeZombie();
    return;
  }

  if (ngnt->GetBinning() == nullptr) {
    NLogger::Error("NGnTree::NGnTree: Binning in NGnTree is nullptr.");
    MakeZombie();
    return;
  }

  // TODO: Import binning from user
  fBinning     = (NBinning *)ngnt->GetBinning()->Clone();
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
}

NGnTree::NGnTree(NBinning * b, NStorageTree * s) : TObject(), fBinning(b), fTreeStorage(s)
{
  ///
  /// Constructor
  ///
  if (fBinning == nullptr) {
    NLogger::Error("NGnTree::NGnTree: Binning is nullptr.");
    MakeZombie();
  }
  if (s == nullptr) {
    fTreeStorage = new NStorageTree(fBinning);
    fTreeStorage->InitTree("", "hnst");
  }

  if (fTreeStorage == nullptr) {
    NLogger::Error("NGnTree::NGnTree: Storage tree is nullptr.");
    MakeZombie();
  }

  // fBinning->Initialize();
  //
  // TODO: Check if this is needed
  fTreeStorage->SetBinning(fBinning);
  fBinning->GetPoint()->SetTreeStorage(fTreeStorage);
}

NGnTree::~NGnTree()
{
  ///
  /// Destructor
  ///

  SafeDelete(fBinning);
}
void NGnTree::Print(Option_t * option) const
{
  ///
  /// Print object
  ///

  if (fBinning) {
    fBinning->Print(option);
  }
  else {
    NLogger::Error("Binning is not initialized in NGnTree !!!");
  }
  if (fTreeStorage) {
    fTreeStorage->Print(option);
  }
  else {
    NLogger::Error("Storage tree is not initialized in NGnTree !!!");
  }
}

bool NGnTree::Process(NHnSparseProcessFuncPtr func, const json & cfg, std::string binningName)
{
  ///
  /// Process the sparse object with the given function
  ///

  if (!fBinning) {
    NLogger::Error("Binning is not initialized in NGnTree !!!");
    return false;
  }

  NBinning * hnsbBinningIn = (NBinning *)fBinning->Clone();

  std::vector<std::string> defNames = fBinning->GetDefinitionNames();
  if (!binningName.empty()) {
    // Check if binning definitions exist
    if (std::find(defNames.begin(), defNames.end(), binningName) == defNames.end()) {
      NLogger::Error("Binning definition '%s' not found in NGnTree !!!", binningName.c_str());
      return false;
    }
    defNames.clear();
    defNames.push_back(binningName);
  }

  fBinning->Reset();
  fBinning->SetCfg(cfg); // Set configuration to binning point
  bool rc = Process(func, defNames, cfg, hnsbBinningIn);
  if (!rc) {
    NLogger::Error("NGnTree::Process: Processing failed !!!");
    return false;
  }
  // bool rc = false;
  return true;
}

bool NGnTree::Process(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames, const json & cfg,
                      NBinning * hnsbBinningIn)
{
  ///
  /// Process the sparse object with the given function
  ///

  NLogger::Info("NGnTree::Process: Starting processing with %zu definitions ...", defNames.size());
  bool batch = gROOT->IsBatch();
  gROOT->SetBatch(kTRUE);
  int nThreads = ROOT::GetThreadPoolSize(); // Get the number of threads to use

  // Disable TH1 add directory feature
  TH1::AddDirectory(kFALSE);

  // Initialize output list
  TList *       output = GetOutput(fBinning->GetCurrentDefinitionName());
  NTreeBranch * b      = fTreeStorage->GetBranch("outputPoint");
  if (!b) fTreeStorage->AddBranch("outputPoint", nullptr, "TList");

  // fBinning->GetPoint()->SetTreeStorage(fTreeStorage); // Set the storage tree to the binning point

  // Original binning
  NBinning * originalBinning = (NBinning *)hnsbBinningIn->Clone();

  // check if ImplicitMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {

    NLogger::Info("NGnTree::Process: ImplicitMT is enabled, using %zu threads", nThreads);
    // 1. Create the vector of NThreadData objects
    std::vector<Ndmspc::NGnThreadData> threadDataVector(nThreads);
    std::string                        filePrefix = fTreeStorage->GetPrefix() + "/";
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      std::string filename =
          filePrefix + std::to_string(gSystem->GetPid()) + "_" + std::to_string(i) + "_" + fTreeStorage->GetPostfix();
      bool rc = threadDataVector[i].Init(i, func, this, hnsbBinningIn, filename, fTreeStorage->GetTree()->GetName());
      if (!rc) {
        Ndmspc::NLogger::Error("Failed to initialize thread data %zu, exiting ...", i);
        return false;
      }
      threadDataVector[i].SetCfg(cfg); // Set configuration to binning point
    }
    auto start_par = std::chrono::high_resolution_clock::now();
    auto task      = [](const std::vector<int> & coords, Ndmspc::NGnThreadData & thread_obj) {
      // NLogger::Warning("Processing coordinates %s in thread %zu", NUtils::GetCoordsString(coords).c_str(),
      //                       thread_obj.GetAssignedIndex());
      // thread_obj.Print();
      thread_obj.Process(coords);
    };

    int iDef   = 0;
    int sumIds = 0;

    for (auto & name : defNames) {
      // Get the binning definition
      // auto binningDef = fBinning->GetDefinition(name);
      auto binningDef = hnsbBinningIn->GetDefinition(name);
      if (!binningDef) {
        NLogger::Error("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
        return false;
      }
      // hnsbBinningIn->GetDefinition(name);

      // binningDef->Print();

      // Convert the binning definition to mins and maxs
      std::vector<int> mins, maxs;

      mins.push_back(0);
      maxs.push_back(binningDef->GetIds().size() - 1);

      NLogger::Debug("NGnTree::Process: Processing with binning definition '%s' with %zu entries", name.c_str(),
                     binningDef->GetIds().size());

      for (size_t i = 0; i < threadDataVector.size(); ++i) {
        threadDataVector[i].GetHnSparseBase()->GetBinning()->SetCurrentDefinitionName(name);
      }
      /// Main execution
      Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
      executorMT.ExecuteParallel<Ndmspc::NGnThreadData>(task, threadDataVector);

      // Update hnsbBinningIn with the processed ids
      NLogger::Trace("NGnTree::Process: [BEGIN] ------------------------------------------------");
      sumIds += hnsbBinningIn->GetDefinition(name)->GetIds().size();
      hnsbBinningIn->GetDefinition(name)->GetIds().clear();
      for (size_t i = 0; i < threadDataVector.size(); ++i) {
        NLogger::Trace("NGnTree::Process: -> Thread %zu processed %lld entries", i,
                       threadDataVector[i].GetNProcessed());
        // threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->Print();
        hnsbBinningIn->GetDefinition(name)->GetIds().insert(
            hnsbBinningIn->GetDefinition(name)->GetIds().end(),
            threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().begin(),
            threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().end());
        sort(hnsbBinningIn->GetDefinition(name)->GetIds().begin(), hnsbBinningIn->GetDefinition(name)->GetIds().end());
      }
      // hnsbBinningIn->GetDefinition(name)->Print();
      // remove entries present in hnsbBinningIn from other definitions
      for (size_t i = 0; i < defNames.size(); i++) {

        std::string other_name = defNames[i];
        auto        otherDef   = hnsbBinningIn->GetDefinition(other_name);
        if (i <= iDef) {
          continue;
        }
        if (!otherDef) {
          NLogger::Error("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", other_name.c_str());
          return false;
        }
        // remove entries that has value less then sumIds
        for (auto it = otherDef->GetIds().begin(); it != otherDef->GetIds().end();) {
          NLogger::Trace("NGnTree::Process: Checking entry %lld from definition '%s' against sumIds=%d", *it,
                         other_name.c_str(), sumIds);
          if (*it < sumIds) {
            NLogger::Trace("NGnTree::Process: Removing entry %lld from definition '%s'", *it, other_name.c_str());
            it = otherDef->GetIds().erase(it);
          }
          else {
            ++it;
          }
        }

        hnsbBinningIn->GetDefinition(other_name)->Print();
      }
      // hnsbBinningIn->GetDefinition(name)->Print();
      iDef++;

      NLogger::Trace("NGnTree::Process: [END] ------------------------------------------------");
      // return false;
    }
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    Ndmspc::NLogger::Info("NGnTree::Process: Parallel execution completed and it took %s .",
                          NUtils::FormatTime(par_duration.count() / 1000).c_str());

    //
    // Print number of results
    Ndmspc::NLogger::Info("NGnTree::Process: Post processing %zu results ...", threadDataVector.size());
    for (auto & data : threadDataVector) {
      Ndmspc::NLogger::Trace("NGnTree::Process: Result from thread %zu: ", data.GetAssignedIndex());
      data.GetHnSparseBase()->GetStorageTree()->Close(true);
    }

    // NLogger::Info("Merging results was skipped !!!");
    // return false;

    Ndmspc::NLogger::Debug("NGnTree::Process: Merging %zu results ...", threadDataVector.size());
    TList *                 mergeList  = new TList();
    Ndmspc::NGnThreadData * outputData = new Ndmspc::NGnThreadData();
    outputData->Init(0, func, this, hnsbBinningIn);
    outputData->SetCfg(cfg);
    // outputData->Init(0, func, this);

    for (auto & data : threadDataVector) {
      Ndmspc::NLogger::Trace("NGnTree::Process: Adding thread data %zu to merge list ...", data.GetAssignedIndex());
      // data.GetHnSparseBase()->GetBinning()->GetPoint()->SetCfg(cfg);
      mergeList->Add(&data);
    }

    Long64_t nmerged = outputData->Merge(mergeList);
    if (nmerged <= 0) {
      Ndmspc::NLogger::Error("NGnTree::Process: Failed to merge thread data, exiting ...");
      delete mergeList;
      return false;
    }
    // return false;
    // TIter next(outputData->GetOutput());
    // while (auto obj = next()) {
    //   Ndmspc::NLogger::Trace("Adding object '%s' to binning '%s' ...", obj->GetName(),
    //                          fBinning->GetCurrentDefinitionName().c_str());
    //   GetOutput()->Add(obj);
    // }
    // NLogger::Info("Output list contains %d objects", GetOutput()->GetEntries());
    // outputData->GetTreeStorage()->Close(true); // Close the output file and write the tree
    fTreeStorage = outputData->GetHnSparseBase()->GetStorageTree();
    fOutputs     = outputData->GetHnSparseBase()->GetOutputs();
    fBinning     = outputData->GetHnSparseBase()->GetBinning(); // Update binning to the merged one
    Ndmspc::NLogger::Info("NGnTree::Process: Merged %lld outputs successfully", nmerged);
  }
  else {

    // int lastIdx = 0;
    for (auto & name : defNames) {
      // Get the binning definition
      auto binningDef = fBinning->GetDefinition(name);
      if (!binningDef) {
        NLogger::Error("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
        return false;
      }

      hnsbBinningIn->GetDefinition(name);
      // hnsbBinningIn->Print();
      // binningDef->Print();

      // Convert the binning definition to mins and maxs
      std::vector<int> mins, maxs;

      mins.push_back(0);
      maxs.push_back(binningDef->GetIds().size() - 1);
      // lastIdx += binningDef->GetIds().size();
      int refreshRate = maxs[0] / 100;

      NLogger::Trace("NGnTree::Process Processing with binning definition '%s' with %zu entries", name.c_str(),
                     binningDef->GetIds().size());

      // binningDef->Print();
      binningDef->GetIds().clear();
      binningDef->GetContent()->Reset(); // Reset the content before processing

      // rc = Process(func, mins, maxs, binningDef, cfg);
      // Print();
      // std::vector<Long64_t> entries;

      auto start_par = std::chrono::high_resolution_clock::now();
      auto task      = [this, func, binningDef, start_par, &maxs, &refreshRate,
                   hnsbBinningIn](const std::vector<int> & coords) {
        NBinningPoint * point = fBinning->GetPoint();
        // Long64_t        entry = binnigcngDef->GetId(coords[0]);
        // Long64_t entry = hnsbBinningIn->GetDefinition(binningDef->GetName())->GetId(coords[0]);
        Long64_t entry = hnsbBinningIn->GetDefinition()->GetId(coords[0]);

        if (entry < fBinning->GetContent()->GetNbins()) {
          // entry = binningDef->GetContent()->GetBinContent(entry);
          // point->SetEntryNumber(entry);
          // FIXME: Handle this case properly (this should be done in the end of whole processing)
          // fBinning->GetDefinition()->GetIds().push_back(entry);
          NLogger::Trace("NGnTree::Process: Skipping entry=%lld, because it was already process !!!", entry);
          return;
        }
        // hnsbBinningIn->GetContent()->GetBinContent(coords[0]);
        //
        // Ndmspc::NLogger::Debug("coords=[%lld] Binning definition ID: '%s' hnsbBinningIn_entry=%lld", coords[0],
        //                        hnsbBinningIn->GetCurrentDefinitionName().c_str(), entry);
        hnsbBinningIn->GetContent()->GetBinContent(entry, point->GetCoords());
        point->RecalculateStorageCoords(entry, false);

        TList * outputPoint = new TList();

        // if (coords[0] % refreshRate == 0) {
        //   NUtils::ProgressBar(coords[0], maxs[0], 50, "Processing entries");
        // }

        func(point, this->GetOutput(), outputPoint, 0); // Call the lambda function
        if (outputPoint->GetEntries() > 0) {
          // NLogger::Debug("NGnTree::Process: Processed entry %lld -> coords=[%lld] Binning definition ID: '%s' "
          //                "hnsbBinningIn_entry=%lld output=%lld",
          //                point->GetEntryNumber(), coords[0], hnsbBinningIn->GetCurrentDefinitionName().c_str(),
          //                entry, outputPoint->GetEntries());
          fTreeStorage->GetBranch("outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
          Int_t bytes = fTreeStorage->Fill(point, nullptr, false, {}, false);
          if (bytes > 0) {
            if (point->GetEntryNumber() == 0 && fTreeStorage->GetEntries() > 1) {
              Ndmspc::NLogger::Error("NGnTree::Process: [!!!Should not happen!!!] entry number is zero: point=%lld "
                                               "entries=%lld",
                                          point->GetEntryNumber(), fTreeStorage->GetEntries());
            }
            fBinning->GetDefinition()->GetIds().push_back(point->GetEntryNumber());
          }
          else {
            Ndmspc::NLogger::Error(
                "NGnTree::Process: [!!!Should not happen!!!] Failed to fill storage tree for entry %lld", entry);
          }
        }
        else {
          // Ndmspc::NLogger::Debug("NGnTree::Process: No output !!! -> coords=[%lld] Binning definition ID: '%s' "
          //                        "hnsbBinningIn_entry=%lld output=%lld",
          //                        coords[0], hnsbBinningIn->GetCurrentDefinitionName().c_str(), entry,
          //                        outputPoint->GetEntries());
        }

        NLogger::Trace("NGnTree::Process: outputPoint contains %d objects", outputPoint->GetEntries());

        // for (Int_t i = 0; i < outputPoint->GetEntries(); ++i) {
        //   TObject * obj = outputPoint->At(i);
        //   if (obj) {
        //     // obj->SetDirectory(nullptr); // Detach from any directory to avoid memory leaks
        //     outputPoint->Remove(obj); // Remove if already exists
        //     delete obj;               // Delete the object to avoid memory leaks
        //   }
        // }

        // Clear the list to avoid memory leaks
        for (auto obj : *outputPoint) {
          delete obj;
        }
        outputPoint->Clear();
        delete outputPoint; // Clean up the output list
      };

      // Main execution
      Ndmspc::NDimensionalExecutor executor(mins, maxs);
      executor.Execute(task);
    }

    std::cout << std::endl;
    NLogger::Debug("NGnTree::Process: Executor is finished for all binning definitions ...");

    // Loop over binning definitions and merge their contents
    fTreeStorage->SetEnabledBranches({}, 0);
    for (const auto & name : fBinning->GetDefinitionNames()) {
      NBinningDef * binningDef = fBinning->GetDefinition(name);
      if (!binningDef) {
        NLogger::Error("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
        continue;
      }
      // binningDef->Print();

      // continue;
      // Recalculate binningDef content based on ids
      // binningDef->GetContent()->Reset();
      std::vector<Long64_t> ids;
      for (auto id : binningDef->GetIds()) {
        GetEntry(id, false);
        // fBinning->GetPoint()->Print("");
        Long64_t bin = binningDef->GetContent()->GetBin(fBinning->GetPoint()->GetStorageCoords(), false);
        NLogger::Trace("NGnTree::Process: -> Setting content bin %lld to id %lld", bin, id);
        if (bin < 0) {
          NLogger::Error("NGnTree::Process: Failed to get bin for id %lld, skipping ...", id);
          continue;
        }
        ids.push_back(id);
        // fBinning->GetPoint()->Print("");
        // binningDef->GetContent()->SetBinContent(bin, id);
      }
      binningDef->GetIds() = ids;
    }

    fBinning->GetPoint()->Reset();
    fTreeStorage->SetEnabledBranches({}, 1);
  }

  NLogger::Trace("NGnTree::Process: Final binning definition update ...");
  // Fill ids from previous binning to the current one and check also presence in hnsbBinningIn
  // loop over indexes in hnsbBinningIn and check if they are present in fBinning
  for (size_t i = 0; i < defNames.size() - 1; ++i) {
    const auto & name = defNames[i];
    NLogger::Trace("NGnTree::Process: Updating entries to definition '%s' from previous definition", name.c_str());
    for (size_t j = i + 1; j < defNames.size(); ++j) {
      auto other_name = defNames[j];
      NLogger::Trace("NGnTree::Process: Checking entries from definition '%s' -> '%s'", name.c_str(),
                     other_name.c_str());
      // fBinning->GetDefinition(name)->Print();
      // originalBinning->GetDefinition(other_name)->Print();
      // fBinning->GetDefinition(other_name)->Print();

      // Get ids in reverse order to put them in the beginning
      for (auto it = originalBinning->GetDefinition(other_name)->GetIds().rbegin();
           it != originalBinning->GetDefinition(other_name)->GetIds().rend(); ++it) {
        auto id = *it;
        // check if id is present in fBinning->GetDefinition(name)
        if (std::find(fBinning->GetDefinition(name)->GetIds().begin(), fBinning->GetDefinition(name)->GetIds().end(),
                      id) != fBinning->GetDefinition(name)->GetIds().end()) {
          NLogger::Trace("NGnTree::Process: -> Adding id %lld to definition '%s'", id, other_name.c_str());
          // put it to the beginning
          fBinning->GetDefinition(other_name)
              ->GetIds()
              .insert(fBinning->GetDefinition(other_name)->GetIds().begin(), id);
        }
      }
    }
  }

  // NLogger::Trace("NGnTree::Process: Finalizing ordering from histogram storage definition ...");
  // for (const auto & name : fBinning->GetDefinitionNames()) {
  //   auto binningDef = fBinning->GetDefinition(name);
  //   if (!binningDef) {
  //     NLogger::Error("Binning definition '%s' not found in NGnTree !!!", name.c_str());
  //     continue;
  //   }
  //   binningDef->Print();
  //   // Recalculate binningDef content based on ids
  //   binningDef->GetContent()->Reset();
  //   std::vector<Long64_t> ids;
  //   for (auto id : binningDef->GetIds()) {
  //     GetEntry(id, false);
  //     Long64_t bin = binningDef->GetContent()->GetBin(fBinning->GetPoint()->GetStorageCoords(), false);
  //     if (bin < 0) {
  //       NLogger::Error("Failed to get bin for id %lld, skipping ...", id);
  //       continue;
  //     }
  //     ids.push_back(id);
  //     binningDef->GetContent()->SetBinContent(bin, id);
  //     NLogger::Debug("-> Setting content bin %lld to id %lld", bin, id);
  //   }
  //   binningDef->GetIds() = ids;
  // }

  NLogger::Info("NGnTree::Process: Printing final binning definitions:");
  for (const auto & name : fBinning->GetDefinitionNames()) {
    fBinning->GetDefinition(name)->RefreshContentfomIds();
    fBinning->GetDefinition(name)->Print();
  }

  gROOT->SetBatch(batch); // Restore ROOT batch mode
  return true;
}
TList * NGnTree::GetOutput(std::string name)
{
  ///
  /// Get output list for the given binning definition name
  ///

  if (name.empty()) {
    name = fBinning->GetCurrentDefinitionName();
  }
  if (fOutputs.find(name) == fOutputs.end()) {
    fOutputs[name] = new TList();
    fOutputs[name]->SetName(name.c_str());
  }
  return fOutputs[name];
}

NGnTree * NGnTree::Open(const std::string & filename, const std::string & branches, const std::string & treename)
{
  ///
  /// Open NGnTree from file
  ///

  NLogger::Debug("Opening '%s' with branches='%s' and treename='%s' ...", filename.c_str(), branches.c_str(),
                 treename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    NLogger::Error("NGnTree::Open: Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    NLogger::Error("NGnTree::Open: Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
    return nullptr;
  }

  return Open(tree, branches, file);
}

NGnTree * NGnTree::Open(TTree * tree, const std::string & branches, TFile * file)
{
  ///
  /// Open NGnTree from existing tree
  ///

  NBinning * hnstBinning = (NBinning *)tree->GetUserInfo()->At(0);
  if (!hnstBinning) {
    NLogger::Error("NGnTree::Open: Cannot get binning from tree '%s'", tree->GetName());
    return nullptr;
  }
  NStorageTree * hnstStorageTree = (NStorageTree *)tree->GetUserInfo()->At(1);
  if (!hnstStorageTree) {
    NLogger::Error("NGnTree::Open: Cannot get tree storage info from tree '%s'", tree->GetName());
    return nullptr;
  }

  std::map<std::string, TList *> outputs;
  TDirectory *                   dir = nullptr;
  if (file) {
    dir    = (TDirectory *)file->Get("outputs");
    auto l = dir->GetListOfKeys();
    for (auto kv : *l) {
      TObject * obj = dir->Get(kv->GetName());
      if (!obj) continue;
      TList * l = dynamic_cast<TList *>(obj);
      if (!l) continue;
      outputs[l->GetName()] = l;
      NLogger::Debug("Imported output list for binning '%s' with %d object(s) from file '%s'", l->GetName(),
                     l->GetEntries(), file->GetName());
    }
  }
  // TDirectory * dir = (TDirectory *)tree->GetUserInfo()->FindObject("outputs");
  // if (dir) {
  //   dir->Print();
  // }

  NGnTree * hnst = new NGnTree(hnstBinning, hnstStorageTree);

  if (!hnstStorageTree->SetFileTree(file, tree, true)) return nullptr;
  // if (!hnst->InitBinnings({})) return nullptr;
  // hnst->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::NUtils::Tokenize(branches, ',');
    NLogger::Trace("NGnTree::Open: Enabled branches: %s", NUtils::GetCoordsString(enabledBranches, -1).c_str());
    hnstStorageTree->SetEnabledBranches(enabledBranches);
  }
  else {
    // loop over all branches and set address
    for (auto & kv : hnstStorageTree->GetBranchesMap()) {
      NLogger::Trace("NGnTree::Open: Enabled branches: %s", kv.first.c_str());
    }
  }
  // Set all branches to be read
  hnstStorageTree->SetBranchAddresses();
  hnst->SetOutputs(outputs);

  return hnst;
}

bool NGnTree::Close(bool write)
{
  ///
  /// Close the storage tree
  ///

  if (!fTreeStorage) {
    NLogger::Error("NGnTree::Close: Storage tree is not initialized in NGnTree !!!");
    return false;
  }

  return fTreeStorage->Close(write, fOutputs);
}

Int_t NGnTree::GetEntry(Long64_t entry, bool checkBinningDef)
{
  ///
  /// Get entry
  ///
  if (!fTreeStorage) {
    NLogger::Error("NGnTree::GetEntry: Storage tree is not initialized in NGnTree !!!");
    return -1;
  }

  return fTreeStorage->GetEntry(entry, fBinning->GetPoint(0, fBinning->GetCurrentDefinitionName()), checkBinningDef);
}

void NGnTree::Play(int timeout, std::string binning, std::vector<int> outputPointIds,
                   std::vector<std::vector<int>> ranges, Option_t * option, std::string ws)
{
  ///
  /// Play the tree
  ///
  TString opt = option;
  opt.ToUpper();

  Ndmspc::NWsClient * client = nullptr;

  if (!ws.empty()) {
    client = new Ndmspc::NWsClient();
    if (!client->Connect(ws)) {
      Ndmspc::NLogger::Error("Failed to connect to '%s' !!!", ws.c_str());
      return;
    }
    Ndmspc::NLogger::Info("Connected to %s", ws.c_str());
  }

  if (binning.empty()) {
    binning = fBinning->GetCurrentDefinitionName();
  }

  NBinningDef * binningDef = fBinning->GetDefinition(binning);
  if (!binningDef) {
    NLogger::Error("NGnTree::Play: Binning definition '%s' not found in NGnTree !!!", binning.c_str());
    NLogger::Error("Available binning definitions:");
    for (auto & name : fBinning->GetDefinitionNames()) {
      if (name == fBinning->GetCurrentDefinitionName())
        NLogger::Error(" [*] %s", name.c_str());
      else
        NLogger::Error(" [ ] %s", name.c_str());
    }
    return;
  }

  THnSparse * bdContent = (THnSparse *)binningDef->GetContent()->Clone();

  std::string bdContentName = TString::Format("bdContent_%s", binning.c_str()).Data();
  // Set axis ranges if provided
  if (!ranges.empty()) NUtils::SetAxisRanges(bdContent, ranges, false, true);

  Long64_t                                        linBin = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{bdContent->CreateIter(true /*use axis range*/)};
  std::vector<Long64_t>                           ids;
  // std::vector<Long64_t> ids = binningDef->GetIds();
  while ((linBin = iter->Next()) >= 0) {
    // NLogger::Debug("Original content bin %lld: %f", linBin, bdContentOrig->GetBinContent(linBin));
    ids.push_back(linBin);
  }
  if (ids.empty()) {
    NLogger::Warning("NGnTree::Play: No entries found in binning definition '%s' !!!", binning.c_str());
    return;
  }
  // return;

  TCanvas * c1 = nullptr;
  if (!client) {
    c1 = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("c1");
    if (c1 == nullptr) c1 = new TCanvas("c1", "NGnTree::Play", 800, 600);
    c1->Clear();
    c1->cd();
    c1->DivideSquare(outputPointIds.size() > 0 ? outputPointIds.size() + 1 : 1);
    gSystem->ProcessEvents();
  }
  binningDef->Print();
  bdContent->Reset();

  // loop over all ids and print them
  for (auto id : ids) {
    // for (int id = 0; id < GetEntries(); id++) {
    GetEntry(id);
    fBinning->GetPoint()->Print();
    TList * l = (TList *)fTreeStorage->GetBranch("outputPoint")->GetObject();
    if (!l || l->IsEmpty()) {
      NLogger::Info("No output for entry %lld", id);
      continue;
    }
    else {
      // NLogger::Info("Output for entry %lld:", id);
      // l->Print(opt.Data());

      if (outputPointIds.empty()) {
        outputPointIds.resize(l->GetEntries());
        for (int i = 0; i < l->GetEntries(); i++) {
          outputPointIds[i] = i;
        }
      }
      int n = outputPointIds.size();

      if (client) {
        std::string msg = TBufferJSON::ConvertToJSON(l).Data();
        if (!client->Send(msg)) {
          Ndmspc::NLogger::Error("Failed to send message `%s`", msg.c_str());
        }
        else {
          Ndmspc::NLogger::Trace("Sent: %s", msg.c_str());
        }
      }
      else {

        Double_t v = 1.0;
        for (int i = 0; i < n; i++) {
          // NLogger::Debug("Drawing output object id %d (list index %d) on pad %d", outputPointIds[i], i, i + 1);

          c1->cd(i + 2);
          TObject * obj = l->At(outputPointIds[i]);
          if (obj) {
            // obj->Print();
            obj->Draw();
          }
          if (obj->InheritsFrom(TH1::Class()) && i == 0) {
            TH1 * h = (TH1 *)obj;
            v       = h->GetMean();
            NLogger::Debug("Mean value from histogram [%s]: %f", h->GetName(), v);
          }
        }
        bdContent->SetBinContent(fBinning->GetPoint()->GetStorageCoords(), v);
        c1->cd(1);
        TH1 * bdProj = (TH1 *)gROOT->FindObjectAny("bdProj");
        if (bdProj) {
          delete bdProj;
          bdProj = nullptr;
        }
        if (bdContent->GetNdimensions() == 1) {
          bdProj = bdContent->Projection(0, "O");
        }
        else if (bdContent->GetNdimensions() == 2) {
          bdProj = bdContent->Projection(0, 1, "O");
        }
        else if (bdContent->GetNdimensions() == 3) {
          bdProj = bdContent->Projection(0, 1, 2, "O");
        }
        else {
          NLogger::Error("NGnTree::Play: Cannot project THnSparse with %d dimensions", bdContent->GetNdimensions());
        }
        if (bdProj) {
          bdProj->SetName("bdProj");
          bdProj->SetTitle(TString::Format("Binning '%s' content projection", binning.c_str()).Data());
          bdProj->SetMinimum(0);
          // bdProj->SetDirectory(nullptr);
          bdProj->Draw("colz");
          // c1->ModifiedUpdate();
        }
      }
    }
    if (c1) c1->ModifiedUpdate();
    gSystem->ProcessEvents();
    if (timeout > 0) gSystem->Sleep(timeout);
    NLogger::Info("%d", id);
  }
  if (client) client->Disconnect();

  delete bdContent;
}

} // namespace Ndmspc
