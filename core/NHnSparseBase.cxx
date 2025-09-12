#include <TList.h>
#include <TROOT.h>
#include <vector>
#include "TTree.h"
#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NHnSparseThreadData.h"
#include "NLogger.h"
#include "NTreeBranch.h"
#include "NUtils.h"
#include "NStorageTree.h"
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
  fTreeStorage = new NStorageTree();
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
  fTreeStorage = new NStorageTree();
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
  if (fTreeStorage == nullptr) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: Storage tree is nullptr.");
    MakeZombie();
  }
  // fBinning->Initialize();
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

  bool rc = false;
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
    rc = Process(func, mins, maxs, binningDef, cfg);
    if (!rc) {
      NLogger::Error("Processing with binning definition '%s' failed !!!", name.c_str());
      return false;
    }
  }
  return true;
}

bool NHnSparseBase::Process(NHnSparseProcessFuncPtr func, std::vector<int> mins, std::vector<int> maxs,
                            NBinningDef * binningDef, const json & cfg)
{
  ///
  /// Process the sparse object with the given function
  ///

  bool batch = gROOT->IsBatch();
  gROOT->SetBatch(kTRUE);
  int nThreads = ROOT::GetThreadPoolSize(); // Get the number of threads to use

  // Initialize output list
  TList * output = GetOutput(fBinning->GetCurrentDefinitionName());
  // Disable TH1 add directory feature
  TH1::AddDirectory(kFALSE);

  // check if ImplicitMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {

    NLogger::Info("ImplicitMT is enabled, using %zu threads", nThreads);
    Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
    // 1. Create the vector of NThreadData objects
    std::vector<Ndmspc::NHnSparseThreadData> threadDataVector(nThreads);
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      threadDataVector[i].SetAssignedIndex(i);
      threadDataVector[i].SetProcessFunc(func); // Set the processing function
      threadDataVector[i].SetBinning(fBinning);
      threadDataVector[i].SetBinningDef(binningDef);
      threadDataVector[i].InitStorage();
      // thread_data_vector[i].GetHnstOutput(hnstOut);       // Set the input HnSparseTree
      // thread_data_vector[i].SetOutput(new TList()); // Set global output list
      // thread_data_vector[i].GetHnSparseTree(); // Initialize the NHnSparseTree
    }

    auto start_par = std::chrono::high_resolution_clock::now();

    auto task = [](const std::vector<int> & coords, Ndmspc::NHnSparseThreadData & thread_obj) {
      thread_obj.Print();
      thread_obj.Process(coords);
    };

    executorMT.ExecuteParallel<Ndmspc::NHnSparseThreadData>(task, threadDataVector);
    auto                                      end_par      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

    Ndmspc::NLogger::Info("Parallel execution completed and it took %.3f s", par_duration.count() / 1000);
    //
    // Print number of results
    Ndmspc::NLogger::Info("Post processing %zu results ...", threadDataVector.size());
    for (auto & data : threadDataVector) {
      Ndmspc::NLogger::Trace("Result from thread %zu: ", data.GetAssignedIndex());
    }

    Ndmspc::NLogger::Info("Merging %zu results ...", threadDataVector.size());
    TList *                       mergeList  = new TList();
    Ndmspc::NHnSparseThreadData * outputData = new Ndmspc::NHnSparseThreadData();

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

    TIter next(outputData->GetOutput());
    while (auto obj = next()) {
      Ndmspc::NLogger::Trace("Adding object '%s' to binning '%s' ...", obj->GetName(),
                             fBinning->GetCurrentDefinitionName().c_str());
      GetOutput()->Add(obj);
    }
    NLogger::Info("Output list contains %d objects", GetOutput()->GetEntries());
  }
  else {

    Ndmspc::NDimensionalExecutor executor(mins, maxs);

    NTreeBranch * b = fTreeStorage->GetBranch("output");
    if (!b) fTreeStorage->AddBranch("output", nullptr, "TList");

    // Print();
    auto task = [this, func, binningDef](const std::vector<int> & coords) {
      Long64_t entry = 0;
      if (binningDef) {
        entry = binningDef->GetId(coords[0]); // Get the binning definition ID for the first axis
        Ndmspc::NLogger::Debug("Binning definition ID: %lld", entry);
        fBinning->GetContent()->GetBinContent(
            entry, fBinning->GetPoint()->GetCoords()); // Get the bin content for the given entry
      }

      TList * outputPoint = new TList();
      fBinning->GetPoint()->RecalculateStorageCoords();
      func(fBinning->GetPoint(), this->GetOutput(), outputPoint, 0); // Call the lambda function

      fTreeStorage->GetBranch("output")->SetAddress(outputPoint); // Set the output list as branch address
      fTreeStorage->Fill(fBinning->GetPoint(), nullptr, {}, false);
      delete outputPoint; // Clean up the output list
    };
    executor.Execute(task);
  }

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
    // fOutputs[name]->SetOwner(true);
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

  // TODO: Check if this is needed
  // hnst->GetPoint()->SetHnSparseTree(hnst);

  return hnst;
}

bool NHnSparseBase::Close(bool write)
{
  ///
  /// Close the storage tree
  ///

  if (!fTreeStorage) {
    NLogger::Error("Storage tree is not initialized in NHnSparseBase !!!");
    return false;
  }
  return fTreeStorage->Close(write, fBinning);
}

Int_t NHnSparseBase::GetEntry(Long64_t entry)
{
  ///
  /// Get entry
  ///
  if (!fTreeStorage) {
    NLogger::Error("NHnSparseBase::GetEntry: Storage tree is not initialized in NHnSparseBase !!!");
    return -1;
  }

  return fTreeStorage->GetEntry(entry, fBinning->GetPoint(0, fBinning->GetCurrentDefinitionName()));
}

} // namespace Ndmspc
