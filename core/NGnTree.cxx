#include <cstddef>
#include <string>
#include <vector>
#include <iostream>
#include <TDirectory.h>
#include <TObject.h>
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
#include <sys/poll.h>
#include "NParameters.h"
#include "NResourceMonitor.h"
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
#include "NGnNavigator.h"
#include "NGnTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnTree);
/// \endcond

namespace Ndmspc {
NGnTree::NGnTree() : TObject() {}
NGnTree::NGnTree(std::vector<TAxis *> axes, std::string filename, std::string treename) : TObject(), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (axes.empty()) {
    NLogError("NGnTree::NGnTree: No axes provided, binning is nullptr.");
    // fBinning = new NBinning();
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}
NGnTree::NGnTree(TObjArray * axes, std::string filename, std::string treename) : TObject(), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (axes == nullptr && axes->GetEntries() == 0) {
    NLogError("NGnTree::NGnTree: No axes provided, binning is nullptr.");
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}

NGnTree::NGnTree(NGnTree * ngnt, std::string filename, std::string treename) : TObject(), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (ngnt == nullptr) {
    NLogError("NGnTree::NGnTree: NGnTree is nullptr.");
    MakeZombie();
    return;
  }

  if (ngnt->GetBinning() == nullptr) {
    NLogError("NGnTree::NGnTree: Binning in NGnTree is nullptr.");
    MakeZombie();
    return;
  }

  // TODO: Import binning from user
  fBinning     = (NBinning *)ngnt->GetBinning()->Clone();
  fTreeStorage = new NStorageTree(fBinning);
  fTreeStorage->InitTree(filename, treename);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}

NGnTree::NGnTree(NBinning * b, NStorageTree * s) : TObject(), fBinning(b), fTreeStorage(s), fInput(nullptr)
{
  ///
  /// Constructor
  ///
  if (fBinning == nullptr) {
    NLogError("NGnTree::NGnTree: Binning is nullptr.");
    MakeZombie();
  }
  if (s == nullptr) {
    fTreeStorage = new NStorageTree(fBinning);
    fTreeStorage->InitTree("", "ngnt");
  }

  if (fTreeStorage == nullptr) {
    NLogError("NGnTree::NGnTree: Storage tree is nullptr.");
    MakeZombie();
  }

  // fBinning->Initialize();
  //
  // TODO: Check if this is needed
  fTreeStorage->SetBinning(fBinning);
  fBinning->GetPoint()->SetTreeStorage(fTreeStorage);
  fNavigator = new NGnNavigator();
  fNavigator->SetGnTree(this);
}

NGnTree::~NGnTree()
{
  ///
  /// Destructor
  ///

  SafeDelete(fBinning);
  // SafeDelete(fTreeStorage);
  // SafeDelete(fNavigator);
}
void NGnTree::Print(Option_t * option) const
{
  ///
  /// Print object
  ///

  TString opt = option;

  // Print list of axes
  NLogInfo("NGnTree::Print: Printing NGnTree object [ALL] ...");
  if (fBinning) {
    fBinning->Print(option);
  }
  else {
    NLogError("Binning is not initialized in NGnTree !!!");
  }
  if (fTreeStorage) {
    fTreeStorage->Print(option);
  }
  else {
    NLogError("Storage tree is not initialized in NGnTree !!!");
  }
}

void NGnTree::Draw(Option_t * option)
{
  ///
  /// Draw object
  ///

  NLogInfo("NGnTree::Draw: Drawing NGnTree object [not implemented yet]...");
}

bool NGnTree::Process(NHnSparseProcessFuncPtr func, const json & cfg, std::string binningName)
{
  ///
  /// Process the sparse object with the given function
  ///

  if (!fBinning) {
    NLogError("Binning is not initialized in NGnTree !!!");
    return false;
  }

  NBinning * binningIn = (NBinning *)fBinning->Clone();

  std::vector<std::string> defNames = fBinning->GetDefinitionNames();
  if (!binningName.empty()) {
    // Check if binning definitions exist
    if (std::find(defNames.begin(), defNames.end(), binningName) == defNames.end()) {
      NLogError("Binning definition '%s' not found in NGnTree !!!", binningName.c_str());
      return false;
    }
    defNames.clear();
    defNames.push_back(binningName);
  }

  fBinning->Reset();
  fBinning->SetCfg(cfg); // Set configuration to binning point
  // bool rc = ProcessOld(func, defNames, cfg, binningIn);
  bool rc = Process(func, defNames, cfg, binningIn);
  if (!rc) {
    NLogError("NGnTree::Process: Processing failed !!!");
    return false;
  }
  // bool rc = false;
  return true;
}
bool NGnTree::ProcessOld(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames, const json & cfg,
                         NBinning * binningIn)
{
  ///
  /// Process the sparse object with the given function
  ///

  NLogInfo("NGnTree::Process: Starting processing with %zu definitions ...", defNames.size());
  bool batch = gROOT->IsBatch();
  gROOT->SetBatch(kTRUE);
  int nThreads = ROOT::GetThreadPoolSize(); // Get the number of threads to use

  const char * wsUrl = gSystem->Getenv("NDMSPC_WS_URL");
  if (!fWsClient && wsUrl) {
    fWsClient = new NWsClient();
    fWsClient->Connect(wsUrl);
    NLogInfo("NGnTree::Process: Connected to WebSocket server at '%s'", wsUrl);
  }

  // Disable TH1 add directory feature
  TH1::AddDirectory(kFALSE);

  // Initialize output list
  TList *       output = GetOutput(fBinning->GetCurrentDefinitionName());
  NTreeBranch * b      = fTreeStorage->GetBranch("outputPoint");
  if (!b) fTreeStorage->AddBranch("outputPoint", nullptr, "TList");

  if (fParameters) {
    NTreeBranch * b = fTreeStorage->GetBranch("results");
    if (!b) fTreeStorage->AddBranch("results", nullptr, "Ndmspc::NParameters");
  }

  // Original binning
  NBinning * originalBinning = (NBinning *)binningIn->Clone();

  // check if ImplicitMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {

    NLogInfo("NGnTree::Process: ImplicitMT is enabled, using %zu threads", nThreads);
    // 1. Create the vector of NThreadData objects
    std::vector<Ndmspc::NGnThreadData> threadDataVector(nThreads);
    std::string                        filePrefix = fTreeStorage->GetPrefix() + "/";
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      std::string filename =
          filePrefix + std::to_string(gSystem->GetPid()) + "_" + std::to_string(i) + "_" + fTreeStorage->GetPostfix();
      bool rc =
          threadDataVector[i].Init(i, func, this, binningIn, fInput, filename, fTreeStorage->GetTree()->GetName());
      if (!rc) {
        NLogError("Failed to initialize thread data %zu, exiting ...", i);
        return false;
      }
      threadDataVector[i].SetCfg(cfg); // Set configuration to binning point
    }
    auto start_par = std::chrono::high_resolution_clock::now();
    auto task      = [](const std::vector<int> & coords, Ndmspc::NGnThreadData & thread_obj) {
      // NLogWarning("Processing coordinates %s in thread %zu", NUtils::GetCoordsString(coords).c_str(),
      //                       thread_obj.GetAssignedIndex());
      // thread_obj.Print();
      thread_obj.Process(coords);
    };

    int iDef   = 0;
    int sumIds = 0;

    for (auto & name : defNames) {
      // Get the binning definition
      // auto binningDef = fBinning->GetDefinition(name);
      auto binningDef = binningIn->GetDefinition(name);
      if (!binningDef) {
        NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
        return false;
      }
      // hnsbBinningIn->GetDefinition(name);

      // binningDef->Print();

      // Convert the binning definition to mins and maxs
      std::vector<int> mins, maxs;

      mins.push_back(0);
      maxs.push_back(binningDef->GetIds().size() - 1);

      NLogDebug("NGnTree::Process: Processing with binning definition '%s' with %zu entries", name.c_str(),
                binningDef->GetIds().size());

      for (size_t i = 0; i < threadDataVector.size(); ++i) {
        threadDataVector[i].GetHnSparseBase()->GetBinning()->SetCurrentDefinitionName(name);
      }
      /// Main execution
      Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
      executorMT.ExecuteParallel<Ndmspc::NGnThreadData>(task, threadDataVector);

      // Update hnsbBinningIn with the processed ids
      NLogTrace("NGnTree::Process: [BEGIN] ------------------------------------------------");
      sumIds += binningIn->GetDefinition(name)->GetIds().size();
      binningIn->GetDefinition(name)->GetIds().clear();
      for (size_t i = 0; i < threadDataVector.size(); ++i) {
        NLogTrace("NGnTree::Process: -> Thread %zu processed %lld entries", i, threadDataVector[i].GetNProcessed());
        // threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->Print();
        binningIn->GetDefinition(name)->GetIds().insert(
            binningIn->GetDefinition(name)->GetIds().end(),
            threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().begin(),
            threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().end());
        sort(binningIn->GetDefinition(name)->GetIds().begin(), binningIn->GetDefinition(name)->GetIds().end());
      }
      // hnsbBinningIn->GetDefinition(name)->Print();
      // remove entries present in hnsbBinningIn from other definitions
      for (size_t i = 0; i < defNames.size(); i++) {

        std::string other_name = defNames[i];
        auto        otherDef   = binningIn->GetDefinition(other_name);
        if (i <= iDef) {
          continue;
        }
        if (!otherDef) {
          NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", other_name.c_str());
          return false;
        }
        // remove entries that has value less then sumIds
        for (auto it = otherDef->GetIds().begin(); it != otherDef->GetIds().end();) {
          NLogTrace("NGnTree::Process: Checking entry %lld from definition '%s' against sumIds=%d", *it,
                    other_name.c_str(), sumIds);
          if (*it < sumIds) {
            NLogTrace("NGnTree::Process: Removing entry %lld from definition '%s'", *it, other_name.c_str());
            it = otherDef->GetIds().erase(it);
          }
          else {
            ++it;
          }
        }

        binningIn->GetDefinition(other_name)->Print();
      }
      // hnsbBinningIn->GetDefinition(name)->Print();
      iDef++;

      NLogTrace("NGnTree::Process: [END] ------------------------------------------------");
      // return false;
    }
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    NLogInfo("NGnTree::Process: Parallel execution completed and it took %s .",
             NUtils::FormatTime(par_duration.count() / 1000).c_str());

    //
    // Print number of results
    NLogInfo("NGnTree::Process: Post processing %zu results ...", threadDataVector.size());
    for (auto & data : threadDataVector) {
      NLogTrace("NGnTree::Process: Result from thread %zu: ", data.GetAssignedIndex());
      data.GetHnSparseBase()->GetStorageTree()->Close(true);
    }

    // NLogInfo("Merging results was skipped !!!");
    // return false;

    NLogDebug("NGnTree::Process: Merging %zu results ...", threadDataVector.size());
    TList *                 mergeList  = new TList();
    Ndmspc::NGnThreadData * outputData = new Ndmspc::NGnThreadData();
    outputData->Init(0, func, this, binningIn);
    outputData->SetCfg(cfg);
    // outputData->Init(0, func, this);

    for (auto & data : threadDataVector) {
      NLogTrace("NGnTree::Process: Adding thread data %zu to merge list ...", data.GetAssignedIndex());
      // data.GetHnSparseBase()->GetBinning()->GetPoint()->SetCfg(cfg);
      mergeList->Add(&data);
    }

    Long64_t nmerged = outputData->Merge(mergeList);
    if (nmerged <= 0) {
      NLogError("NGnTree::Process: Failed to merge thread data, exiting ...");
      delete mergeList;
      return false;
    }

    // delete all temporary files
    for (auto & data : threadDataVector) {
      std::string filename = data.GetHnSparseBase()->GetStorageTree()->GetFileName();
      NLogTrace("NGnTree::Process: Deleting temporary file '%s' ...", filename.c_str());
      gSystem->Exec(TString::Format("rm -f %s", filename.c_str()));
    }

    // return false;
    // TIter next(outputData->GetOutput());
    // while (auto obj = next()) {
    //   NLogTrace("Adding object '%s' to binning '%s' ...", obj->GetName(),
    //                          fBinning->GetCurrentDefinitionName().c_str());
    //   GetOutput()->Add(obj);
    // }
    // NLogInfo("Output list contains %d objects", GetOutput()->GetEntries());
    // outputData->GetTreeStorage()->Close(true); // Close the output file and write the tree
    fTreeStorage = outputData->GetHnSparseBase()->GetStorageTree();
    fOutputs     = outputData->GetHnSparseBase()->GetOutputs();
    fBinning     = outputData->GetHnSparseBase()->GetBinning(); // Update binning to the merged one
    fParameters  = outputData->GetHnSparseBase()->GetParameters();
    NLogInfo("NGnTree::Process: Merged %lld outputs successfully", nmerged);
  }
  else {

    if (fParameters) {
      fBinning->GetPoint()->SetParameters(fParameters);
      fTreeStorage->GetBranch("results")->SetAddress(fParameters); // Set the output list as branch address
    }
    // int lastIdx = 0;
    for (auto & name : defNames) {
      // Get the binning definition
      auto binningDef = fBinning->GetDefinition(name);
      if (!binningDef) {
        NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
        return false;
      }

      binningIn->GetDefinition(name);
      // hnsbBinningIn->Print();
      // binningDef->Print();

      // Convert the binning definition to mins and maxs
      std::vector<int> mins, maxs;

      mins.push_back(0);
      maxs.push_back(binningDef->GetIds().size() - 1);
      // lastIdx += binningDef->GetIds().size();
      int refreshRate = maxs[0] / 100;

      NLogTrace("NGnTree::Process Processing with binning definition '%s' with %zu entries", name.c_str(),
                binningDef->GetIds().size());

      // binningDef->Print();
      binningDef->GetIds().clear();
      binningDef->GetContent()->Reset(); // Reset the content before processing

      // rc = Process(func, mins, maxs, binningDef, cfg);
      // Print();
      // std::vector<Long64_t> entries;

      // Create stat histogram for processed entries

      NResourceMonitor monitor;
      output->Add(monitor.Initialize(binningDef->GetContent())); // Add stat histogram to output list

      json jobMon;
      jobMon["jobName"] = name;
      jobMon["user"]    = gSystem->Getenv("USER");
      jobMon["host"]    = gSystem->HostName();
      jobMon["pid"]     = gSystem->GetPid();
      jobMon["ntasks"]  = maxs[0] + 1;
      if (fWsClient) {
        NLogInfo("NGnTree::Process: Sending job monitor info to WebSocket server ...");
        fWsClient->Send(jobMon.dump());
      }
      NLogInfo("NGnTree::Process: Starting processing for binning definition '%s' with %d tasks ...", name.c_str(),
               maxs[0] + 1);

      auto start_par = std::chrono::high_resolution_clock::now();
      auto task      = [this, func, binningDef, start_par, &maxs, &refreshRate, binningIn,
                   &monitor](const std::vector<int> & coords) {
        NBinningPoint * point = fBinning->GetPoint();
        Long64_t        entry = binningIn->GetDefinition()->GetId(coords[0]);
        json            progress;
        progress["jobName"] = binningDef->GetName();
        progress["status"]  = "R";
        progress["task"]    = coords[0];
        if (fWsClient) {
          fWsClient->Send(progress.dump());
          NLogDebug("NGnTree::Process: Sent progress for task %d", coords[0]);
        }

        if (entry < fBinning->GetContent()->GetNbins()) {
          // entry = binningDef->GetContent()->GetBinContent(entry);
          // point->SetEntryNumber(entry);
          // FIXME: Handle this case properly (this should be done in the end of whole processing)
          // fBinning->GetDefinition()->GetIds().push_back(entry);
          NLogTrace("NGnTree::Process: Skipping entry=%lld, because it was already process !!!", entry);
          return;
        }
        // hnsbBinningIn->GetContent()->GetBinContent(coords[0]);
        //
        // NLogDebug("coords=[%lld] Binning definition ID: '%s' hnsbBinningIn_entry=%lld", coords[0],
        //                        hnsbBinningIn->GetCurrentDefinitionName().c_str(), entry);
        binningIn->GetContent()->GetBinContent(entry, point->GetCoords());
        point->RecalculateStorageCoords(entry, false);

        TList * outputPoint = new TList();

        // if (coords[0] % refreshRate == 0) {
        //   NUtils::ProgressBar(coords[0], maxs[0], 50, "Processing entries");
        // }

        point->SetTreeStorage(fTreeStorage); // Set the storage tree to the binning point
        point->SetInput(fInput);             // Set the input NGnTree to the binning point

        // gather start resource usage
        monitor.Start();

        // Process the point
        func(point, this->GetOutput(), outputPoint, 0); // Call the lambda function

        // gather resource usage after processing
        monitor.End();
        monitor.Fill(point->GetStorageCoords(), 0);
        // print resource usage
        // monitor.Print();

        // accept only non-empty output points
        if (outputPoint->GetEntries() > 0) {
          // NLogDebug("NGnTree::Process: Processed entry %lld -> coords=[%lld] Binning definition ID: '%s' "
          //                "hnsbBinningIn_entry=%lld output=%lld",
          //                point->GetEntryNumber(), coords[0], hnsbBinningIn->GetCurrentDefinitionName().c_str(),
          //                entry, outputPoint->GetEntries());
          fTreeStorage->GetBranch("outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
          Int_t bytes = fTreeStorage->Fill(point, nullptr, false, {}, false);
          if (bytes > 0) {
            if (point->GetEntryNumber() == 0 && fTreeStorage->GetEntries() > 1) {
              NLogError("NGnTree::Process: [!!!Should not happen!!!] entry number is zero: point=%lld "
                                  "entries=%lld",
                             point->GetEntryNumber(), fTreeStorage->GetEntries());
            }
            fBinning->GetDefinition()->GetIds().push_back(point->GetEntryNumber());
            progress["status"] = "D";
          }
          else {
            NLogError("NGnTree::Process: [!!!Should not happen!!!] Failed to fill storage tree for entry %lld", entry);
            progress["status"] = "F";
          }
        }
        else {
          progress["status"] = "S";
          // NLogDebug("NGnTree::Process: No output !!! -> coords=[%lld] Binning definition ID: '%s' "
          //                        "hnsbBinningIn_entry=%lld output=%lld",
          //                        coords[0], hnsbBinningIn->GetCurrentDefinitionName().c_str(), entry,
          //                        outputPoint->GetEntries());
        }

        NLogTrace("NGnTree::Process: outputPoint contains %d objects", outputPoint->GetEntries());

        if (fWsClient) {
          fWsClient->Send(progress.dump());
          NLogDebug("NGnTree::Process: Sent done for task %d", coords[0]);
        }
        // for (Int_t i = 0; i < outputPoint->GetEntries(); ++i) {
        //   TObject * obj = outputPoint->At(i);
        //   if (obj) {
        //     // obj->SetDirectory(nullptr); // Detach from any directory to avoid memory leaks
        //     outputPoint->Remove(obj); // Remove if already exists
        //     delete obj;               // Delete the object to avoid memory leaks
        //   }
        // }

        // Clear the list to avoid memory leaks
        // for (auto obj : *outputPoint) {
        //   delete obj;
        // }
        outputPoint->SetOwner(kTRUE); // Set owner to true to delete contained objects
        outputPoint->Clear();
        delete outputPoint; // Clean up the output list
      };

      // Main execution
      Ndmspc::NDimensionalExecutor executor(mins, maxs);
      executor.Execute(task);
    }

    std::cout << std::endl;
    NLogDebug("NGnTree::Process: Executor is finished for all binning definitions ...");

    // Loop over binning definitions and merge their contents
    fTreeStorage->SetEnabledBranches({}, 0);
    for (const auto & name : fBinning->GetDefinitionNames()) {
      NBinningDef * binningDef = fBinning->GetDefinition(name);
      if (!binningDef) {
        NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
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
        NLogTrace("NGnTree::Process: -> Setting content bin %lld to id %lld", bin, id);
        if (bin < 0) {
          NLogError("NGnTree::Process: Failed to get bin for id %lld, skipping ...", id);
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

  NLogTrace("NGnTree::Process: Final binning definition update ...");
  // Fill ids from previous binning to the current one and check also presence in hnsbBinningIn
  // loop over indexes in hnsbBinningIn and check if they are present in fBinning
  for (size_t i = 0; i < defNames.size() - 1; ++i) {
    const auto & name = defNames[i];
    NLogTrace("NGnTree::Process: Updating entries to definition '%s' from previous definition", name.c_str());
    for (size_t j = i + 1; j < defNames.size(); ++j) {
      auto other_name = defNames[j];
      NLogTrace("NGnTree::Process: Checking entries from definition '%s' -> '%s'", name.c_str(), other_name.c_str());
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
          NLogTrace("NGnTree::Process: -> Adding id %lld to definition '%s'", id, other_name.c_str());
          // put it to the beginning
          fBinning->GetDefinition(other_name)
              ->GetIds()
              .insert(fBinning->GetDefinition(other_name)->GetIds().begin(), id);
        }
      }
    }
  }

  // NLogTrace("NGnTree::Process: Finalizing ordering from histogram storage definition ...");
  // for (const auto & name : fBinning->GetDefinitionNames()) {
  //   auto binningDef = fBinning->GetDefinition(name);
  //   if (!binningDef) {
  //     NLogError("Binning definition '%s' not found in NGnTree !!!", name.c_str());
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
  //       NLogError("Failed to get bin for id %lld, skipping ...", id);
  //       continue;
  //     }
  //     ids.push_back(id);
  //     binningDef->GetContent()->SetBinContent(bin, id);
  //     NLogDebug("-> Setting content bin %lld to id %lld", bin, id);
  //   }
  //   binningDef->GetIds() = ids;
  // }

  NLogInfo("NGnTree::Process: Printing final binning definitions:");
  for (const auto & name : fBinning->GetDefinitionNames()) {
    fBinning->GetDefinition(name)->RefreshContentFromIds();
    fBinning->GetDefinition(name)->Print();
  }

  if (fWsClient) {
    // gSystem->Sleep(1000); // wait for messages to be sent
    fWsClient->Disconnect();
    SafeDelete(fWsClient);
  }

  gROOT->SetBatch(batch); // Restore ROOT batch mode
  return true;
}

bool NGnTree::Process(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames, const json & cfg,
                      NBinning * binningIn)
{
  ///
  /// Process the sparse object with the given function
  ///

  NLogInfo("NGnTree::Process: Starting processing with %zu definitions ...", defNames.size());
  bool batch = gROOT->IsBatch();
  gROOT->SetBatch(kTRUE);
  TH1::AddDirectory(kFALSE);

  NUtils::EnableMT();

  int nThreads = ROOT::GetThreadPoolSize(); // Get the number of threads to use
  if (nThreads < 1) nThreads = 1;
  const char * wsUrl = gSystem->Getenv("NDMSPC_WS_URL");
  if (!fWsClient && wsUrl) {
    fWsClient = new NWsClient();
    fWsClient->Connect(wsUrl);
    NLogInfo("NGnTree::Process: Connected to WebSocket server at '%s'", wsUrl);
  }

  std::vector<Ndmspc::NGnThreadData> threadDataVector(nThreads);
  std::string jobDir     = fTreeStorage->GetPrefix() + "/.ndmspc/tmp/" + std::to_string(gSystem->GetPid());
  std::string filePrefix = jobDir;
  for (size_t i = 0; i < threadDataVector.size(); ++i) {
    std::string filename = filePrefix + "/" + std::to_string(i) + "/" + fTreeStorage->GetPostfix();
    bool rc = threadDataVector[i].Init(i, func, this, binningIn, fInput, filename, fTreeStorage->GetTree()->GetName());
    if (!rc) {
      NLogError("Failed to initialize thread data %zu, exiting ...", i);
      return false;
    }
    threadDataVector[i].SetCfg(cfg); // Set configuration to binning point
  }
  auto task = [](const std::vector<int> & coords, Ndmspc::NGnThreadData & thread_obj) {
    // NLogWarning("Processing coordinates %s in thread %zu", NUtils::GetCoordsString(coords).c_str(),
    //                       thread_obj.GetAssignedIndex());
    // thread_obj.Print();
    json progress;
    progress["jobName"]  = thread_obj.GetHnSparseBase()->GetBinning()->GetCurrentDefinitionName();
    progress["status"]   = "R";
    progress["task"]     = coords[0];
    NWsClient * wsClient = thread_obj.GetHnSparseBase()->GetWsClient();
    if (wsClient) wsClient->Send(progress.dump());
    thread_obj.Process(coords);
    progress["status"] = "D";
    if (wsClient) wsClient->Send(progress.dump());
  };
  int iDef   = 0;
  int sumIds = 0;

  auto start_par = std::chrono::high_resolution_clock::now();
  for (auto & name : defNames) {
    auto binningDef = binningIn->GetDefinition(name);
    if (!binningDef) {
      NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
      return false;
    }
    std::vector<int> mins, maxs;
    mins.push_back(0);
    maxs.push_back(binningDef->GetIds().size() - 1);
    NLogDebug("NGnTree::Process: Processing with binning definition '%s' with %zu entries", name.c_str(),
              binningDef->GetIds().size());

    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      threadDataVector[i].GetHnSparseBase()->GetBinning()->SetCurrentDefinitionName(name);
    }
    json jobMon;
    jobMon["jobName"] = name;
    jobMon["user"]    = gSystem->Getenv("USER");
    jobMon["host"]    = gSystem->HostName();
    jobMon["pid"]     = gSystem->GetPid();
    jobMon["status"]  = "I";
    jobMon["ntasks"]  = maxs[0] + 1;
    if (fWsClient) {
      NLogInfo("NGnTree::Process: Sending job monitor info to WebSocket server ...");
      fWsClient->Send(jobMon.dump());
    }
    /// Main execution
    Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
    executorMT.ExecuteParallel<Ndmspc::NGnThreadData>(task, threadDataVector);

    // Update hnsbBinningIn with the processed ids
    NLogDebug("NGnTree::Process: [BEGIN] ------------------------------------------------");
    sumIds += binningIn->GetDefinition(name)->GetIds().size();
    binningIn->GetDefinition(name)->GetIds().clear();
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      NLogDebug("NGnTree::Process: -> Thread %zu processed %lld entries", i, threadDataVector[i].GetNProcessed());
      // threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->Print();
      binningIn->GetDefinition(name)->GetIds().insert(
          binningIn->GetDefinition(name)->GetIds().end(),
          threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().begin(),
          threadDataVector[i].GetHnSparseBase()->GetBinning()->GetDefinition(name)->GetIds().end());
      sort(binningIn->GetDefinition(name)->GetIds().begin(), binningIn->GetDefinition(name)->GetIds().end());
    }
    // hnsbBinningIn->GetDefinition(name)->Print();
    // remove entries present in hnsbBinningIn from other definitions
    for (size_t i = 0; i < defNames.size(); i++) {

      std::string other_name = defNames[i];
      auto        otherDef   = binningIn->GetDefinition(other_name);
      if (i <= iDef) {
        continue;
      }
      if (!otherDef) {
        NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", other_name.c_str());
        return false;
      }
      // remove entries that has value less then sumIds
      for (auto it = otherDef->GetIds().begin(); it != otherDef->GetIds().end();) {
        NLogDebug("NGnTree::Process: Checking entry %lld from definition '%s' against sumIds=%d", *it,
                  other_name.c_str(), sumIds);
        if (*it < sumIds) {
          NLogDebug("NGnTree::Process: Removing entry %lld from definition '%s'", *it, other_name.c_str());
          it = otherDef->GetIds().erase(it);
        }
        else {
          ++it;
        }
      }

      binningIn->GetDefinition(other_name)->Print();
    }
    // hnsbBinningIn->GetDefinition(name)->Print();
    iDef++;

    NLogDebug("NGnTree::Process: [END] ------------------------------------------------");
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    NLogInfo("NGnTree::Process: Execution completed and it took %s .",
             NUtils::FormatTime(par_duration.count() / 1000).c_str());

    NLogInfo("NGnTree::Process: Post processing %zu results ...", threadDataVector.size());
    for (auto & data : threadDataVector) {
      NLogTrace("NGnTree::Process: Result from thread %zu: ", data.GetAssignedIndex());
      data.GetHnSparseBase()->GetStorageTree()->Close(true);
    }

    NLogDebug("NGnTree::Process: Merging %zu results ...", threadDataVector.size());
    TList *                 mergeList  = new TList();
    Ndmspc::NGnThreadData * outputData = new Ndmspc::NGnThreadData();
    outputData->Init(0, func, this, binningIn);
    outputData->SetCfg(cfg);
    // outputData->Init(0, func, this);

    for (auto & data : threadDataVector) {
      NLogTrace("NGnTree::Process: Adding thread data %zu to merge list ...", data.GetAssignedIndex());
      // data.GetHnSparseBase()->GetBinning()->GetPoint()->SetCfg(cfg);
      mergeList->Add(&data);
    }

    Long64_t nmerged = outputData->Merge(mergeList);
    if (nmerged <= 0) {
      NLogError("NGnTree::Process: Failed to merge thread data, exiting ...");
      delete mergeList;
      return false;
    }
    NLogInfo("NGnTree::Process: Merged %lld outputs successfully", nmerged);
    // delete all temporary files
    // for (auto & data : threadDataVector) {
    //   std::string filename = data.GetHnSparseBase()->GetStorageTree()->GetFileName();
    //   NLogTrace("NGnTree::Process: Deleting temporary file '%s' ...", filename.c_str());
    //   gSystem->Exec(TString::Format("rm -f %s", filename.c_str()));
    // }
    gSystem->Exec(TString::Format("rm -fr %s", jobDir.c_str()));
    fTreeStorage     = outputData->GetHnSparseBase()->GetStorageTree();
    fOutputs         = outputData->GetHnSparseBase()->GetOutputs();
    fBinning         = outputData->GetHnSparseBase()->GetBinning(); // Update binning to the merged one
    fParameters      = outputData->GetHnSparseBase()->GetParameters();
    jobMon["status"] = "D";
    if (fWsClient) {
      fWsClient->Send(jobMon.dump());
    }
  }

  if (fWsClient) {
    // gSystem->Sleep(1000); // wait for messages to be sent
    fWsClient->Disconnect();
    SafeDelete(fWsClient);
  }
  //
  // // Initialize output list
  // TList *       output = GetOutput(fBinning->GetCurrentDefinitionName());
  // NTreeBranch * b      = fTreeStorage->GetBranch("outputPoint");
  // if (!b) fTreeStorage->AddBranch("outputPoint", nullptr, "TList");
  //
  // if (fParameters) {
  //   NTreeBranch * b = fTreeStorage->GetBranch("results");
  //   if (!b) fTreeStorage->AddBranch("results", nullptr, "Ndmspc::NParameters");
  // }
  //
  // // Original binning
  // NBinning * originalBinning = (NBinning *)binningIn->Clone();
  //
  // // check if ImplicitMT is enabled
  // if (ROOT::IsImplicitMTEnabled()) {
  //

  // TODO: AAA

  // }
  // else {
  //
  //   if (fParameters) {
  //     fBinning->GetPoint()->SetParameters(fParameters);
  //     fTreeStorage->GetBranch("results")->SetAddress(fParameters); // Set the output list as branch address
  //   }
  //   // int lastIdx = 0;
  //   for (auto & name : defNames) {
  //     // Get the binning definition
  //     auto binningDef = fBinning->GetDefinition(name);
  //     if (!binningDef) {
  //       NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
  //       return false;
  //     }
  //
  //     binningIn->GetDefinition(name);
  //     // hnsbBinningIn->Print();
  //     // binningDef->Print();
  //
  //     // Convert the binning definition to mins and maxs
  //     std::vector<int> mins, maxs;
  //
  //     mins.push_back(0);
  //     maxs.push_back(binningDef->GetIds().size() - 1);
  //     // lastIdx += binningDef->GetIds().size();
  //     int refreshRate = maxs[0] / 100;
  //
  //     NLogTrace("NGnTree::Process Processing with binning definition '%s' with %zu entries", name.c_str(),
  //               binningDef->GetIds().size());
  //
  //     // binningDef->Print();
  //     binningDef->GetIds().clear();
  //     binningDef->GetContent()->Reset(); // Reset the content before processing
  //
  //     // rc = Process(func, mins, maxs, binningDef, cfg);
  //     // Print();
  //     // std::vector<Long64_t> entries;
  //
  //     // Create stat histogram for processed entries
  //
  //     NResourceMonitor monitor;
  //     output->Add(monitor.Initialize(binningDef->GetContent())); // Add stat histogram to output list
  //
  //     NLogInfo("NGnTree::Process: Starting processing for binning definition '%s' with %d tasks ...", name.c_str(),
  //              maxs[0] + 1);
  //
  //     auto start_par = std::chrono::high_resolution_clock::now();
  //     auto task      = [this, func, binningDef, start_par, &maxs, &refreshRate, binningIn,
  //                  &monitor](const std::vector<int> & coords) {
  //       NBinningPoint * point = fBinning->GetPoint();
  //       Long64_t        entry = binningIn->GetDefinition()->GetId(coords[0]);
  //       json            progress;
  //       progress["jobName"] = binningDef->GetName();
  //       progress["status"]  = "R";
  //       progress["task"]    = coords[0];
  //       if (fWsClient) {
  //         fWsClient->Send(progress.dump());
  //         NLogDebug("NGnTree::Process: Sent progress for task %d", coords[0]);
  //       }
  //
  //       if (entry < fBinning->GetContent()->GetNbins()) {
  //         // entry = binningDef->GetContent()->GetBinContent(entry);
  //         // point->SetEntryNumber(entry);
  //         // FIXME: Handle this case properly (this should be done in the end of whole processing)
  //         // fBinning->GetDefinition()->GetIds().push_back(entry);
  //         NLogTrace("NGnTree::Process: Skipping entry=%lld, because it was already process !!!", entry);
  //         return;
  //       }
  //       // hnsbBinningIn->GetContent()->GetBinContent(coords[0]);
  //       //
  //       // NLogDebug("coords=[%lld] Binning definition ID: '%s' hnsbBinningIn_entry=%lld", coords[0],
  //       //                        hnsbBinningIn->GetCurrentDefinitionName().c_str(), entry);
  //       binningIn->GetContent()->GetBinContent(entry, point->GetCoords());
  //       point->RecalculateStorageCoords(entry, false);
  //
  //       TList * outputPoint = new TList();
  //
  //       // if (coords[0] % refreshRate == 0) {
  //       //   NUtils::ProgressBar(coords[0], maxs[0], 50, "Processing entries");
  //       // }
  //
  //       point->SetTreeStorage(fTreeStorage); // Set the storage tree to the binning point
  //       point->SetInput(fInput);             // Set the input NGnTree to the binning point
  //
  //       // gather start resource usage
  //       monitor.Start();
  //
  //       // Process the point
  //       func(point, this->GetOutput(), outputPoint, 0); // Call the lambda function
  //
  //       // gather resource usage after processing
  //       monitor.End();
  //       monitor.Fill(point->GetStorageCoords(), 0);
  //       // print resource usage
  //       // monitor.Print();
  //
  //       // accept only non-empty output points
  //       if (outputPoint->GetEntries() > 0) {
  //         // NLogDebug("NGnTree::Process: Processed entry %lld -> coords=[%lld] Binning definition ID: '%s' "
  //         //                "hnsbBinningIn_entry=%lld output=%lld",
  //         //                point->GetEntryNumber(), coords[0], hnsbBinningIn->GetCurrentDefinitionName().c_str(),
  //         //                entry, outputPoint->GetEntries());
  //         fTreeStorage->GetBranch("outputPoint")->SetAddress(outputPoint); // Set the output list as branch address
  //         Int_t bytes = fTreeStorage->Fill(point, nullptr, false, {}, false);
  //         if (bytes > 0) {
  //           if (point->GetEntryNumber() == 0 && fTreeStorage->GetEntries() > 1) {
  //             NLogError("NGnTree::Process: [!!!Should not happen!!!] entry number is zero: point=%lld "
  //                                 "entries=%lld",
  //                            point->GetEntryNumber(), fTreeStorage->GetEntries());
  //           }
  //           fBinning->GetDefinition()->GetIds().push_back(point->GetEntryNumber());
  //           progress["status"] = "D";
  //         }
  //         else {
  //           NLogError("NGnTree::Process: [!!!Should not happen!!!] Failed to fill storage tree for entry %lld",
  //           entry); progress["status"] = "F";
  //         }
  //       }
  //       else {
  //         progress["status"] = "S";
  //         // NLogDebug("NGnTree::Process: No output !!! -> coords=[%lld] Binning definition ID: '%s' "
  //         //                        "hnsbBinningIn_entry=%lld output=%lld",
  //         //                        coords[0], hnsbBinningIn->GetCurrentDefinitionName().c_str(), entry,
  //         //                        outputPoint->GetEntries());
  //       }
  //
  //       NLogTrace("NGnTree::Process: outputPoint contains %d objects", outputPoint->GetEntries());
  //
  //       if (fWsClient) {
  //         fWsClient->Send(progress.dump());
  //         NLogDebug("NGnTree::Process: Sent done for task %d", coords[0]);
  //       }
  //       // for (Int_t i = 0; i < outputPoint->GetEntries(); ++i) {
  //       //   TObject * obj = outputPoint->At(i);
  //       //   if (obj) {
  //       //     // obj->SetDirectory(nullptr); // Detach from any directory to avoid memory leaks
  //       //     outputPoint->Remove(obj); // Remove if already exists
  //       //     delete obj;               // Delete the object to avoid memory leaks
  //       //   }
  //       // }
  //
  //       // Clear the list to avoid memory leaks
  //       // for (auto obj : *outputPoint) {
  //       //   delete obj;
  //       // }
  //       outputPoint->SetOwner(kTRUE); // Set owner to true to delete contained objects
  //       outputPoint->Clear();
  //       delete outputPoint; // Clean up the output list
  //     };
  //
  //     // Main execution
  //     Ndmspc::NDimensionalExecutor executor(mins, maxs);
  //     executor.Execute(task);
  //   }
  //
  //   std::cout << std::endl;
  //   NLogDebug("NGnTree::Process: Executor is finished for all binning definitions ...");
  //
  //   // Loop over binning definitions and merge their contents
  //   fTreeStorage->SetEnabledBranches({}, 0);
  //   for (const auto & name : fBinning->GetDefinitionNames()) {
  //     NBinningDef * binningDef = fBinning->GetDefinition(name);
  //     if (!binningDef) {
  //       NLogError("NGnTree::Process: Binning definition '%s' not found in NGnTree !!!", name.c_str());
  //       continue;
  //     }
  //     // binningDef->Print();
  //
  //     // continue;
  //     // Recalculate binningDef content based on ids
  //     // binningDef->GetContent()->Reset();
  //     std::vector<Long64_t> ids;
  //     for (auto id : binningDef->GetIds()) {
  //       GetEntry(id, false);
  //       // fBinning->GetPoint()->Print("");
  //       Long64_t bin = binningDef->GetContent()->GetBin(fBinning->GetPoint()->GetStorageCoords(), false);
  //       NLogTrace("NGnTree::Process: -> Setting content bin %lld to id %lld", bin, id);
  //       if (bin < 0) {
  //         NLogError("NGnTree::Process: Failed to get bin for id %lld, skipping ...", id);
  //         continue;
  //       }
  //       ids.push_back(id);
  //       // fBinning->GetPoint()->Print("");
  //       // binningDef->GetContent()->SetBinContent(bin, id);
  //     }
  //     binningDef->GetIds() = ids;
  //   }
  //
  //   fBinning->GetPoint()->Reset();
  //   fTreeStorage->SetEnabledBranches({}, 1);
  // }
  //
  // NLogTrace("NGnTree::Process: Final binning definition update ...");
  // // Fill ids from previous binning to the current one and check also presence in hnsbBinningIn
  // // loop over indexes in hnsbBinningIn and check if they are present in fBinning
  // for (size_t i = 0; i < defNames.size() - 1; ++i) {
  //   const auto & name = defNames[i];
  //   NLogTrace("NGnTree::Process: Updating entries to definition '%s' from previous definition", name.c_str());
  //   for (size_t j = i + 1; j < defNames.size(); ++j) {
  //     auto other_name = defNames[j];
  //     NLogTrace("NGnTree::Process: Checking entries from definition '%s' -> '%s'", name.c_str(), other_name.c_str());
  //     // fBinning->GetDefinition(name)->Print();
  //     // originalBinning->GetDefinition(other_name)->Print();
  //     // fBinning->GetDefinition(other_name)->Print();
  //
  //     // Get ids in reverse order to put them in the beginning
  //     for (auto it = originalBinning->GetDefinition(other_name)->GetIds().rbegin();
  //          it != originalBinning->GetDefinition(other_name)->GetIds().rend(); ++it) {
  //       auto id = *it;
  //       // check if id is present in fBinning->GetDefinition(name)
  //       if (std::find(fBinning->GetDefinition(name)->GetIds().begin(), fBinning->GetDefinition(name)->GetIds().end(),
  //                     id) != fBinning->GetDefinition(name)->GetIds().end()) {
  //         NLogTrace("NGnTree::Process: -> Adding id %lld to definition '%s'", id, other_name.c_str());
  //         // put it to the beginning
  //         fBinning->GetDefinition(other_name)
  //             ->GetIds()
  //             .insert(fBinning->GetDefinition(other_name)->GetIds().begin(), id);
  //       }
  //     }
  //   }
  // }
  //
  // // NLogTrace("NGnTree::Process: Finalizing ordering from histogram storage definition ...");
  // // for (const auto & name : fBinning->GetDefinitionNames()) {
  // //   auto binningDef = fBinning->GetDefinition(name);
  // //   if (!binningDef) {
  // //     NLogError("Binning definition '%s' not found in NGnTree !!!", name.c_str());
  // //     continue;
  // //   }
  // //   binningDef->Print();
  // //   // Recalculate binningDef content based on ids
  // //   binningDef->GetContent()->Reset();
  // //   std::vector<Long64_t> ids;
  // //   for (auto id : binningDef->GetIds()) {
  // //     GetEntry(id, false);
  // //     Long64_t bin = binningDef->GetContent()->GetBin(fBinning->GetPoint()->GetStorageCoords(), false);
  // //     if (bin < 0) {
  // //       NLogError("Failed to get bin for id %lld, skipping ...", id);
  // //       continue;
  // //     }
  // //     ids.push_back(id);
  // //     binningDef->GetContent()->SetBinContent(bin, id);
  // //     NLogDebug("-> Setting content bin %lld to id %lld", bin, id);
  // //   }
  // //   binningDef->GetIds() = ids;
  // // }
  //
  // NLogInfo("NGnTree::Process: Printing final binning definitions:");
  // for (const auto & name : fBinning->GetDefinitionNames()) {
  //   fBinning->GetDefinition(name)->RefreshContentFromIds();
  //   fBinning->GetDefinition(name)->Print();
  // }
  //

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

  NLogDebug("Opening '%s' with branches='%s' and treename='%s' ...", filename.c_str(), branches.c_str(),
            treename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    NLogError("NGnTree::Open: Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    NLogError("NGnTree::Open: Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
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
    NLogError("NGnTree::Open: Cannot get binning from tree '%s'", tree->GetName());
    return nullptr;
  }
  NStorageTree * hnstStorageTree = (NStorageTree *)tree->GetUserInfo()->At(1);
  if (!hnstStorageTree) {
    NLogError("NGnTree::Open: Cannot get tree storage info from tree '%s'", tree->GetName());
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
      NLogDebug("Imported output list for binning '%s' with %d object(s) from file '%s'", l->GetName(), l->GetEntries(),
                file->GetName());
    }
  }
  // TDirectory * dir = (TDirectory *)tree->GetUserInfo()->FindObject("outputs");
  // if (dir) {
  //   dir->Print();
  // }

  NGnTree * ngnt = new NGnTree(hnstBinning, hnstStorageTree);

  if (!hnstStorageTree->SetFileTree(file, tree, true)) return nullptr;
  // if (!ngnt->InitBinnings({})) return nullptr;
  // ngnt->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::NUtils::Tokenize(branches, ',');
    NLogTrace("NGnTree::Open: Enabled branches: %s", NUtils::GetCoordsString(enabledBranches, -1).c_str());
    hnstStorageTree->SetEnabledBranches(enabledBranches);
  }
  else {
    // loop over all branches and set address
    for (auto & kv : hnstStorageTree->GetBranchesMap()) {
      NLogTrace("NGnTree::Open: Enabled branches: %s", kv.first.c_str());
    }
  }
  // Set all branches to be read
  hnstStorageTree->SetBranchAddresses();
  ngnt->SetOutputs(outputs);

  NGnNavigator * nav = new NGnNavigator();
  nav->SetGnTree(ngnt);
  ngnt->SetNavigator(nav);

  return ngnt;
}

void NGnTree::SetNavigator(NGnNavigator * navigator)
{
  ///
  /// Set navigator
  ///

  if (fNavigator) {
    NLogWarning("NGnTree::SetNavigator: Replacing existing navigator ...");
    SafeDelete(fNavigator);
  }

  fNavigator = navigator;
}

bool NGnTree::Close(bool write)
{
  ///
  /// Close the storage tree
  ///

  if (!fTreeStorage) {
    NLogError("NGnTree::Close: Storage tree is not initialized in NGnTree !!!");
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
    NLogError("NGnTree::GetEntry: Storage tree is not initialized in NGnTree !!!");
    return -1;
  }

  int bytes =
      fTreeStorage->GetEntry(entry, fBinning->GetPoint(0, fBinning->GetCurrentDefinitionName()), checkBinningDef);
  if (fTreeStorage->GetBranch("results")) fParameters = (NParameters *)fTreeStorage->GetBranch("results")->GetObject();
  return bytes;
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
      NLogError("Failed to connect to '%s' !!!", ws.c_str());
      return;
    }
    NLogInfo("Connected to %s", ws.c_str());
  }

  if (binning.empty()) {
    binning = fBinning->GetCurrentDefinitionName();
  }

  NBinningDef * binningDef = fBinning->GetDefinition(binning);
  if (!binningDef) {
    NLogError("NGnTree::Play: Binning definition '%s' not found in NGnTree !!!", binning.c_str());
    NLogError("Available binning definitions:");
    for (auto & name : fBinning->GetDefinitionNames()) {
      if (name == fBinning->GetCurrentDefinitionName())
        NLogError(" [*] %s", name.c_str());
      else
        NLogError(" [ ] %s", name.c_str());
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
    // NLogDebug("Original content bin %lld: %f", linBin, bdContentOrig->GetBinContent(linBin));
    ids.push_back(linBin);
  }
  if (ids.empty()) {
    NLogWarning("NGnTree::Play: No entries found in binning definition '%s' !!!", binning.c_str());
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
      NLogInfo("No output for entry %lld", id);
      continue;
    }
    else {
      // NLogInfo("Output for entry %lld:", id);
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
          NLogError("Failed to send message `%s`", msg.c_str());
        }
        else {
          NLogTrace("Sent: %s", msg.c_str());
        }
      }
      else {

        Double_t v = 1.0;
        for (int i = 0; i < n; i++) {
          // NLogDebug("Drawing output object id %d (list index %d) on pad %d", outputPointIds[i], i, i + 1);

          c1->cd(i + 2);
          TObject * obj = l->At(outputPointIds[i]);
          if (obj) {
            // obj->Print();
            obj->Draw();
          }
          if (obj->InheritsFrom(TH1::Class()) && i == 0) {
            TH1 * h = (TH1 *)obj;
            v       = h->GetMean();
            NLogDebug("Mean value from histogram [%s]: %f", h->GetName(), v);
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
          NLogError("NGnTree::Play: Cannot project THnSparse with %d dimensions", bdContent->GetNdimensions());
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
    NLogInfo("%d", id);
  }
  if (client) client->Disconnect();

  delete bdContent;
}

TList * NGnTree::Projection(const json & cfg, std::string binningName)
{
  ///
  /// Project THnSparse objects from the output lists based on configuration
  ///

  // SetInput(); // Set input to selfp
  SetInput(Ndmspc::NGnTree::Open(fTreeStorage->GetFileName()));
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // NLogInfo("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    point->Print();
    json cfg = point->GetCfg();

    Printf("Processing THnSparse projection with configuration: %s", cfg.dump().c_str());

    Ndmspc::NGnTree * ngntIn = point->GetInput();
    // ngntIn->Print();
    // ngntIn->GetEntry(0);
    ngntIn->GetEntry(point->GetEntryNumber());

    // loop over all cfg["objects"]
    for (auto & [objName, objCfg] : cfg["objects"].items()) {
      NLogInfo("Processing object '%s' ...", objName.c_str());

      THnSparse * hns = (THnSparse *)(ngntIn->GetStorageTree()->GetBranchObject(objName));
      if (hns == nullptr) {
        NLogError("NGnTree::Projection: THnSparse 'hns' not found in storage tree !!!");
        return;
      }
      // hns->Print("all");
      // loop over cfg["objects"][objName] array of projection dimension names
      for (size_t i = 0; i < objCfg.size(); i++) {

        NLogInfo("Processing projection %zu for object '%s' ...", i, objName.c_str());
        std::vector<int>         dims;
        std::vector<std::string> dimNames = cfg["objects"][objName][i].get<std::vector<std::string>>();
        for (const auto & dimName : dimNames) {
          NLogDebug("Looking for dimension name '%s' in THnSparse ...", dimName.c_str());
          int dim = -1;
          for (int i = 0; i < hns->GetNdimensions(); i++) {
            if (dimName == hns->GetAxis(i)->GetName()) {
              dim = i;
              break;
            }
          }
          if (dim >= 0)
            dims.push_back(dim);
          else {
            NLogError("NGnTree::Projection: Dimension name '%s' not found in THnSparse !!!", dimName.c_str());
          }
        }
        // Print dims
        NLogInfo("Projecting THnSparse on dimensions: %s", NUtils::GetCoordsString(dims, -1).c_str());
        TH1 * hPrev = (TH1 *)output->At(i);
        TH1 * hProj = NUtils::ProjectTHnSparse(hns, dims, "O");
        hProj->SetName(TString::Format("%s_proj_%s", objName.c_str(), NUtils::Join(dims, '_').c_str()).Data());
        if (hPrev) {
          hPrev->Add(hProj);
        }
        else {
          output->Add(hProj);
        }
      }
    }
    output->Print();
  };

  // NBinningDef *                 binningDef = fInput->GetBinning()->GetDefinition(binningName);
  NBinningDef * binningDef = GetBinning()->GetDefinition(binningName);
  THnSparse *   hnsIn      = binningDef->GetContent();
  // std::vector<std::vector<int>> ranges{{0, 2, 2}, {2, 1, 1}};
  std::vector<std::vector<int>> ranges = cfg["ranges"].get<std::vector<std::vector<int>>>();
  NUtils::SetAxisRanges(hnsIn, ranges); // Set the ranges for the axes
  Long64_t                                        linBin = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{hnsIn->CreateIter(true /*use axis range*/)};
  std::vector<Long64_t>                           ids;
  // std::vector<Long64_t> ids = binningDef->GetIds();
  while ((linBin = iter->Next()) >= 0) {
    ids.push_back(linBin);
  }
  if (ids.empty()) {
    NLogWarning("NGnTree::Projection: No entries found in binning definition '%s' !!!", binningDef->GetName());
    binningDef->RefreshIdsFromContent();
    return nullptr;
  }

  binningDef->GetIds() = ids;

  // NUtils::SetAxisRanges(, std::vector<std::vector<int>> ranges)
  Process(processFunc, cfg);

  // Refresh binning definition ids from content after processing
  binningDef->RefreshIdsFromContent();

  // GetInput()->Close(false);
  return GetOutput(fBinning->GetCurrentDefinitionName());

  // Close(false);
}

NGnTree * NGnTree::Import(THnSparse * hns, std::string parameterAxis, const std::string & filename)
{
  ///
  /// Import THnSparse as NGnTree and use given parameter axis as result parameter
  ///

  std::map<std::string, std::vector<std::vector<int>>> b;
  TObjArray *                                          axes             = new TObjArray();
  int                                                  parameterAxisIdx = -1;
  std::vector<std::string>                             labels;
  for (int i = 0; i < hns->GetNdimensions(); i++) {
    TAxis * axisIn = (TAxis *)hns->GetAxis(i);
    TAxis * axis   = (TAxis *)axisIn->Clone();

    // check if parameterAxis matches axis name
    if (parameterAxis.compare(axis->GetName()) == 0) {
      parameterAxisIdx = i;
      TAxis * axis     = hns->GetAxis(parameterAxisIdx);
      for (int bin = 1; bin <= axis->GetNbins(); bin++) {
        // NLogInfo("Axis bin %d label: %s", bin, axis->GetBinLabel(bin));
        labels.push_back(axis->GetBinLabel(bin));
      }
      continue;
    }

    // set label
    if (axisIn->IsAlphanumeric()) {
      NLogInfo("Setting axis '%s' labels from input THnSparse", axis->GetName());
      for (int bin = 1; bin <= axisIn->GetNbins(); bin++) {
        std::string label = axisIn->GetBinLabel(bin);
        if (!labels.empty()) axis->SetBinLabel(bin, axisIn->GetBinLabel(bin));
      }
    }

    axes->Add(axis);
    b[axis->GetName()] = {{1}};
  }

  json cfg;
  cfg["parameterAxis"] = parameterAxisIdx;
  cfg["labels"]        = labels;
  // return nullptr;
  NGnTree * ngnt = new NGnTree(axes, filename);

  NGnTree * ngntIn = new NGnTree(axes);
  ngntIn->GetStorageTree()->GetTree()->GetUserInfo()->Add(hns->Clone());
  ngnt->SetInput(ngntIn); // Set input to self

  ngnt->GetBinning()->AddBinningDefinition("default", b);

  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // NLogInfo("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    point->Print();
    json        cfg    = point->GetCfg();
    NGnTree *   ngntIn = point->GetInput();
    THnSparse * hns    = (THnSparse *)ngntIn->GetStorageTree()->GetTree()->GetUserInfo()->At(0);

    int                           axisIdx = cfg["parameterAxis"].get<int>();
    std::vector<std::vector<int>> ranges;
    // set ranges from point storage coords
    int iAxis = 0;
    for (int i = 0; i < hns->GetNdimensions(); i++) {
      if (i == axisIdx) continue; // skip parameter axis
      int coord = point->GetStorageCoords()[iAxis++];
      ranges.push_back({i, coord, coord});
      // NLogInfo("Setting axis %d range to [%d, %d]", i, coord, coord);
    }
    NUtils::SetAxisRanges(hns, ranges);
    TH1 * h = hns->Projection(axisIdx, "O");
    if (h->GetEntries() > 0) {

      std::vector<std::string> labels = cfg["labels"].get<std::vector<std::string>>();

      TH1D * results = new TH1D("results", "Results", labels.size(), 0, labels.size());
      for (size_t i = 0; i < labels.size(); i++) {
        // NLogInfo("Setting results bin %zu label to '%s'", i + 1, labels[i].c_str());
        results->GetXaxis()->SetBinLabel(i + 1, labels[i].c_str());
      }
      for (int bin = 0; bin <= h->GetNbinsX(); bin++) {
        results->SetBinContent(bin, h->GetBinContent(bin));
      }
      outputPoint->Add(results);
      outputPoint->Add(h);
    }
  };

  // NUtils::SetAxisRanges(, std::vector<std::vector<int>> ranges)
  ngnt->Process(processFunc, cfg);
  ngnt->Close(true);
  return ngnt;
}
NGnNavigator * NGnTree::Reshape(std::string binningName, std::vector<std::vector<int>> levels, int level,
                                std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase)
{
  ///
  /// Reshape the navigator
  ///

  NGnNavigator navigator;
  navigator.SetGnTree(this);

  return navigator.Reshape(binningName, levels, level, ranges, rangesBase);
}

NGnNavigator * NGnTree::GetResourceStatisticsNavigator(std::string binningName, std::vector<std::vector<int>> levels,
                                                       int level, std::map<int, std::vector<int>> ranges,
                                                       std::map<int, std::vector<int>> rangesBase)
{
  ///
  /// Get resource statistics navigator
  ///

  if (binningName.empty()) {
    binningName = fBinning->GetCurrentDefinitionName();
  }

  THnSparse * hns = (THnSparse *)fOutputs[binningName]->FindObject("resource_monitor");
  if (!hns) {
    NLogError("NGnTree::Draw: Resource monitor THnSparse not found in outputs !!!");
    return nullptr;
  }
  hns->Print("all");

  auto ngnt = Import(hns, "stat", "/tmp/hnst_imported_for_drawing.root");
  ngnt->Print();
  ngnt->Close();

  auto ngnt2 = NGnTree::Open("/tmp/hnst_imported_for_drawing.root");
  auto nav   = ngnt2->Reshape("default", levels, level, ranges, rangesBase);
  // nav->Export("/tmp/hnst_imported_for_drawing.json", {}, "ws://localhost:8080/ws/root.websocket");
  // nav->Draw();

  return nav;
}

bool NGnTree::InitParameters(const std::vector<std::string> & paramNames)
{
  ///
  /// Initialize parameters
  ///

  if (fParameters) {
    NLogWarning("NGnTree::InitParameters: Replacing existing parameters ...");
    delete fParameters;
  }

  fParameters = new NParameters("results", "Results", paramNames);

  return true;
}

} // namespace Ndmspc
