#include <NStorageTree.h>
#include <TList.h>
#include <TROOT.h>
#include <string>
#include <vector>
#include <THnSparse.h>
#include <TH1.h>
#include <TCanvas.h>
#include <TSystem.h>
#include <TMap.h>
#include <TObjString.h>
#include <TTree.h>
#include <TBufferJSON.h>
#include "NBinning.h"
#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NHnSparseThreadData.h"
#include "NLogger.h"
#include "NTreeBranch.h"
#include "NUtils.h"
#include "NStorageTree.h"
#include "NWsClient.h"
#include "NHnSparseBase.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseBase);
/// \endcond

namespace Ndmspc {
NHnSparseBase::NHnSparseBase() : TObject() {}
NHnSparseBase::NHnSparseBase(std::vector<TAxis *> axes, std::string filename, std::string treename) : TObject()
{
  ///
  /// Constructor
  ///
  if (axes.empty()) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: No axes provided, binning is nullptr.");
    // fBinning = new NBinning();
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  // TODO: Handle filename and treename by user
  fTreeStorage->InitTree(filename, treename);
}
NHnSparseBase::NHnSparseBase(TObjArray * axes, std::string filename, std::string treename) : TObject()
{
  ///
  /// Constructor
  ///
  if (axes == nullptr && axes->GetEntries() == 0) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: No axes provided, binning is nullptr.");
    MakeZombie();
    return;
  }
  fBinning     = new NBinning(axes);
  fTreeStorage = new NStorageTree(fBinning);
  // TODO: Handle filename and treename by user
  fTreeStorage->InitTree(filename, treename);
}
NHnSparseBase::NHnSparseBase(NBinning * b, NStorageTree * s) : TObject(), fBinning(b), fTreeStorage(s)
{
  ///
  /// Constructor
  ///
  if (fBinning == nullptr) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: Binning is nullptr.");
    MakeZombie();
  }
  if (s == nullptr) {
    fTreeStorage = new NStorageTree(fBinning);
    fTreeStorage->InitTree("", "hnst");
  }

  if (fTreeStorage == nullptr) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: Storage tree is nullptr.");
    MakeZombie();
  }

  // fBinning->Initialize();
  //
  // TODO: Check if this is needed
  fTreeStorage->SetBinning(fBinning);
  fBinning->GetPoint()->SetTreeStorage(fTreeStorage);
}

NHnSparseBase::~NHnSparseBase()
{
  ///
  /// Destructor
  ///

  SafeDelete(fBinning);
}
void NHnSparseBase::Print(Option_t * option) const
{
  ///
  /// Print object
  ///

  if (fBinning) {
    fBinning->Print(option);
  }
  else {
    NLogger::Error("Binning is not initialized in NHnSparseBase !!!");
  }
  if (fTreeStorage) {
    fTreeStorage->Print(option);
  }
  else {
    NLogger::Error("Storage tree is not initialized in NHnSparseBase !!!");
  }
}

bool NHnSparseBase::Process(NHnSparseProcessFuncPtr func, const json & cfg, std::string binningName)
{
  ///
  /// Process the sparse object with the given function
  ///

  if (!fBinning) {
    NLogger::Error("Binning is not initialized in NHnSparseBase !!!");
    return false;
  }

  fBinning->SetCfg(cfg); // Set configuration to binning point

  std::vector<std::string> defNames = fBinning->GetDefinitionNames();
  if (!binningName.empty()) {
    // Check if binning definitions exist
    if (std::find(defNames.begin(), defNames.end(), binningName) == defNames.end()) {
      NLogger::Error("Binning definition '%s' not found in NHnSparseBase !!!", binningName.c_str());
      return false;
    }
    defNames.clear();
    defNames.push_back(binningName);
  }

  bool rc = Process(func, defNames, cfg);
  if (!rc) {
    NLogger::Error("NHnSparseBase::Process: Processing failed !!!");
    return false;
  }
  // bool rc = false;
  return true;
}

bool NHnSparseBase::Process(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames, const json & cfg)
{
  ///
  /// Process the sparse object with the given function
  ///

  bool batch = gROOT->IsBatch();
  gROOT->SetBatch(kTRUE);
  int nThreads = ROOT::GetThreadPoolSize(); // Get the number of threads to use

  // Disable TH1 add directory feature
  TH1::AddDirectory(kFALSE);

  // Initialize output list
  TList *       output = GetOutput(fBinning->GetCurrentDefinitionName());
  NTreeBranch * b      = fTreeStorage->GetBranch("output");
  if (!b) fTreeStorage->AddBranch("output", nullptr, "TList");

  // fBinning->GetPoint()->SetTreeStorage(fTreeStorage); // Set the storage tree to the binning point

  // check if ImplicitMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {

    NLogger::Info("ImplicitMT is enabled, using %zu threads", nThreads);
    // 1. Create the vector of NThreadData objects
    std::vector<Ndmspc::NHnSparseThreadData> threadDataVector(nThreads);
    std::string                              filePrefix = fTreeStorage->GetPrefix() + "/";
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      std::string filename =
          filePrefix + std::to_string(gSystem->GetPid()) + "_" + std::to_string(i) + "_" + fTreeStorage->GetPostfix();
      bool rc = threadDataVector[i].Init(i, func, this, filename, fTreeStorage->GetTree()->GetName());
      if (!rc) {
        Ndmspc::NLogger::Error("Failed to initialize thread data %zu, exiting ...", i);
        return false;
      }
    }
    auto start_par = std::chrono::high_resolution_clock::now();
    auto task      = [](const std::vector<int> & coords, Ndmspc::NHnSparseThreadData & thread_obj) {
      // NLogger::Warning("Processing coordinates %s in thread %zu", NUtils::GetCoordsString(coords).c_str(),
      //                  thread_obj.GetAssignedIndex());
      // thread_obj.Print();
      thread_obj.Process(coords);
    };

    for (auto & name : defNames) {
      // Get the binning definition
      auto binningDef = fBinning->GetDefinition(name);
      if (!binningDef) {
        NLogger::Error("Binning definition '%s' not found in NHnSparseBase !!!", name.c_str());
        return false;
      }

      // binningDef->Print();

      // Convert the binning definition to mins and maxs
      std::vector<int> mins, maxs;

      mins.push_back(0);
      maxs.push_back(binningDef->GetIds().size() - 1);

      NLogger::Debug("Processing with binning definition '%s' with %zu entries", name.c_str(),
                     binningDef->GetIds().size());

      for (size_t i = 0; i < threadDataVector.size(); ++i) {
        threadDataVector[i].GetHnSparseBase()->GetBinning()->SetCurrentDefinitionName(name);
      }
      /// Main execution
      Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
      executorMT.ExecuteParallel<Ndmspc::NHnSparseThreadData>(task, threadDataVector);
    }
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    Ndmspc::NLogger::Info("Parallel execution completed and it took %.3f s", par_duration.count() / 1000);
    //
    // Print number of results
    Ndmspc::NLogger::Info("Post processing %zu results ...", threadDataVector.size());
    for (auto & data : threadDataVector) {
      Ndmspc::NLogger::Trace("Result from thread %zu: ", data.GetAssignedIndex());
      data.GetHnSparseBase()->GetStorageTree()->Close(true);
    }

    // return false;

    Ndmspc::NLogger::Info("Merging %zu results ...", threadDataVector.size());
    TList *                       mergeList  = new TList();
    Ndmspc::NHnSparseThreadData * outputData = new Ndmspc::NHnSparseThreadData();
    outputData->Init(0, func, this);
    // outputData->SetBinningDef(binningDef);
    // outputData->InitStorage(fTreeStorage, fTreeStorage->GetFileName());

    for (auto & data : threadDataVector) {
      Ndmspc::NLogger::Trace("Adding thread data %zu to merge list ...", data.GetAssignedIndex());
      mergeList->Add(&data);
    }
    Long64_t nmerged = outputData->Merge(mergeList);
    if (nmerged <= 0) {
      Ndmspc::NLogger::Error("Failed to merge thread data, exiting ...");
      return false;
    }
    Ndmspc::NLogger::Info("Merged %lld outputs successfully", nmerged);

    // TIter next(outputData->GetOutput());
    // while (auto obj = next()) {
    //   Ndmspc::NLogger::Trace("Adding object '%s' to binning '%s' ...", obj->GetName(),
    //                          fBinning->GetCurrentDefinitionName().c_str());
    //   GetOutput()->Add(obj);
    // }
    NLogger::Info("Output list contains %d objects", GetOutput()->GetEntries());
    // outputData->GetTreeStorage()->Close(true); // Close the output file and write the tree
    fTreeStorage = outputData->GetHnSparseBase()->GetStorageTree();
    fOutputs     = outputData->GetHnSparseBase()->GetOutputs();
    fBinning     = outputData->GetHnSparseBase()->GetBinning(); // Update binning to the merged one
  }
  else {

    for (auto & name : defNames) {
      // Get the binning definition
      auto binningDef = fBinning->GetDefinition(name);
      if (!binningDef) {
        NLogger::Error("Binning definition '%s' not found in NHnSparseBase !!!", name.c_str());
        return false;
      }

      // binningDef->Print();

      // Convert the binning definition to mins and maxs
      std::vector<int> mins, maxs;

      mins.push_back(0);
      maxs.push_back(binningDef->GetIds().size() - 1);

      NLogger::Debug("Processing with binning definition '%s' with %zu entries", name.c_str(),
                     binningDef->GetIds().size());
      // rc = Process(func, mins, maxs, binningDef, cfg);
      // Print();
      auto task = [this, func, binningDef](const std::vector<int> & coords) {
        Long64_t        entry = 0;
        NBinningPoint * point = fBinning->GetPoint();
        if (binningDef) {
          entry = binningDef->GetId(coords[0]); // Get the binning definition ID for the first axis
          // Ndmspc::NLogger::Debug("Binning definition ID: %lld", entry);
          fBinning->GetContent()->GetBinContent(entry, point->GetCoords()); // Get the bin content for the given entry
          point->RecalculateStorageCoords(entry, false);
        }

        TList * outputPoint = new TList();
        func(fBinning->GetPoint(), this->GetOutput(), outputPoint, 0); // Call the lambda function

        if (!outputPoint->IsEmpty()) {
          fTreeStorage->GetBranch("output")->SetAddress(outputPoint); // Set the output list as branch address
          fTreeStorage->Fill(fBinning->GetPoint(), nullptr, {}, false);
          outputPoint->Clear(); // Clear the list to avoid memory leaks
        };
        delete outputPoint; // Clean up the output list
      };

      // Main execution
      Ndmspc::NDimensionalExecutor executor(mins, maxs);
      executor.Execute(task);
    }
  }

  fBinning->SetCurrentDefinitionName("default");
  gROOT->SetBatch(batch); // Restore ROOT batch mode
  return true;
}
TList * NHnSparseBase::GetOutput(std::string name)
{
  ///
  /// Get output list for the given binning definition name
  ///

  if (name.empty()) {
    name = fBinning->GetCurrentDefinitionName();
  }
  if (fOutputs.find(name) == fOutputs.end()) {
    fOutputs[name] = new TList();
  }
  return fOutputs[name];
}

NHnSparseBase * NHnSparseBase::Open(const std::string & filename, const std::string & branches,
                                    const std::string & treename)
{
  ///
  /// Open NHnSparseBase from file
  ///

  NLogger::Info("Opening '%s' with branches='%s' and treename='%s' ...", filename.c_str(), branches.c_str(),
                treename.c_str());

  TFile * file = TFile::Open(filename.c_str());
  if (!file) {
    NLogger::Error("NHnSparseBase::Open: Cannot open file '%s'", filename.c_str());
    return nullptr;
  }

  TTree * tree = (TTree *)file->Get(treename.c_str());
  if (!tree) {
    NLogger::Error("NHnSparseBase::Open: Cannot get tree '%s' from file '%s'", treename.c_str(), filename.c_str());
    return nullptr;
  }

  return Open(tree, branches, file);
}

NHnSparseBase * NHnSparseBase::Open(TTree * tree, const std::string & branches, TFile * file)
{
  ///
  /// Open NHnSparseBase from existing tree
  ///

  NBinning * hnstBinning = (NBinning *)tree->GetUserInfo()->At(0);
  if (!hnstBinning) {
    NLogger::Error("NHnSparseBase::Open: Cannot get binning from tree '%s'", tree->GetName());
    return nullptr;
  }
  NStorageTree * hnstStorageTree = (NStorageTree *)tree->GetUserInfo()->At(1);
  if (!hnstStorageTree) {
    NLogger::Error("NHnSparseBase::Open: Cannot get tree storage info from tree '%s'", tree->GetName());
    return nullptr;
  }

  TMap * hnstOutputsMap = (TMap *)tree->GetUserInfo()->FindObject("outputs");

  NHnSparseBase * hnst = new NHnSparseBase(hnstBinning, hnstStorageTree);

  if (!hnstStorageTree->SetFileTree(file, tree, true)) return nullptr;
  // if (!hnst->InitBinnings({})) return nullptr;
  // hnst->Print();
  // Get list of branches
  std::vector<std::string> enabledBranches;
  if (!branches.empty()) {
    enabledBranches = Ndmspc::NUtils::Tokenize(branches, ',');
    NLogger::Trace("Enabled branches: %s", NUtils::GetCoordsString(enabledBranches, -1).c_str());
    hnstStorageTree->SetEnabledBranches(enabledBranches);
  }
  else {
    // loop over all branches and set address
    for (auto & kv : hnstStorageTree->GetBranchesMap()) {
      NLogger::Debug("Enabled branches: %s", kv.first.c_str());
    }
  }
  // Set all branches to be read
  hnstStorageTree->SetBranchAddresses();

  return hnst;
}

bool NHnSparseBase::Close(bool write)
{
  ///
  /// Close the storage tree
  ///

  if (!fTreeStorage) {
    NLogger::Error("NHnSparseBase::Close: Storage tree is not initialized in NHnSparseBase !!!");
    return false;
  }

  return fTreeStorage->Close(write, fOutputs);
}

Int_t NHnSparseBase::GetEntry(Long64_t entry, bool checkBinningDef)
{
  ///
  /// Get entry
  ///
  if (!fTreeStorage) {
    NLogger::Error("NHnSparseBase::GetEntry: Storage tree is not initialized in NHnSparseBase !!!");
    return -1;
  }

  return fTreeStorage->GetEntry(entry, fBinning->GetPoint(0, fBinning->GetCurrentDefinitionName()), checkBinningDef);
}

void NHnSparseBase::Play(int timeout, std::string binning, Option_t * option, std::string ws)
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
    NLogger::Error("NHnSparseBase::Play: Binning definition '%s' not found in NHnSparseBase !!!", binning.c_str());
    NLogger::Error("Available binning definitions:");
    for (auto & name : fBinning->GetDefinitionNames()) {
      if (name == fBinning->GetCurrentDefinitionName())
        NLogger::Error(" [*] %s", name.c_str());
      else
        NLogger::Error(" [ ] %s", name.c_str());
    }
    return;
  }

  TCanvas * c1 = nullptr;
  if (!client) c1 = new TCanvas("c1", "NHnSparseBase::Play", 800, 600);
  binningDef->Print();
  std::vector<Long64_t> ids = binningDef->GetIds();
  // loop over all ids and print them
  for (auto id : ids) {
    GetEntry(id);
    fBinning->GetPoint()->Print();
    TList * l = (TList *)fTreeStorage->GetBranch("output")->GetObject();
    if (!l || l->IsEmpty()) {
      NLogger::Info("No output for entry %lld", id);
      continue;
    }
    else {
      NLogger::Info("Output for entry %lld:", id);
      l->Print(opt.Data());
      TH1 * h = (TH1 *)l->At(0);
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

        if (h) h->Draw();
      }
    }
    if (c1) c1->ModifiedUpdate();
    if (timeout > 0) gSystem->Sleep(timeout);
  }
  if (client) client->Disconnect();
}

} // namespace Ndmspc
