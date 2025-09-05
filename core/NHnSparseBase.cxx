#include <TList.h>
#include <TROOT.h>
#include "NBinningDef.h"
#include "NDimensionalExecutor.h"
#include "NHnSparseThreadData.h"
#include "NLogger.h"
#include "NUtils.h"
#include "RtypesCore.h"
#include "NHnSparseBase.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseBase);
/// \endcond

namespace Ndmspc {
NHnSparseBase::NHnSparseBase() : TObject() {}
NHnSparseBase::NHnSparseBase(std::vector<TAxis *> axes) : TObject()
{
  ///
  /// Constructor
  ///
  if (axes.empty()) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: No axes provided, binning is nullptr.");
    // fBinning = new NBinning();
  }
  else {
    fBinning = new NBinning(axes);
  }
}
NHnSparseBase::NHnSparseBase(TObjArray * axes) : TObject()
{
  ///
  /// Constructor
  ///
  if (axes == nullptr && axes->GetEntries() == 0) {
    NLogger::Error("NHnSparseBase::NHnSparseBase: No axes provided, binning is nullptr.");
    // fBinning = new NBinning();
  }
  else {
    fBinning = new NBinning(axes);
  }
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
}

bool NHnSparseBase::Process(NHnSparseProcessFuncPtr func, std::string binningName, const json & cfg)
{
  ///
  /// Process the sparse object with the given function
  ///

  if (!fBinning) {
    NLogger::Error("Binning is not initialized in NHnSparseBase !!!");
    return false;
  }

  fBinning->SetCfg(cfg); // Set configuration to binning point
  // Get the binning definition
  auto binningDef = fBinning->GetDefinition(binningName);
  if (!binningDef) {
    NLogger::Error("Binning definition '%s' not found in NHnSparseBase !!!", binningName.c_str());
    return false;
  }

  // binningDef->Print();

  // Convert the binning definition to mins and maxs
  std::vector<int> mins, maxs;

  mins.push_back(0);
  maxs.push_back(binningDef->GetIds().size() - 1);

  NLogger::Debug("Processing with binning definition '%s' with %zu entries", binningName.c_str(),
                 binningDef->GetIds().size());
  return Process(func, mins, maxs, binningDef, cfg);
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

  // check if ImplicitMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {

    NLogger::Info("ImplicitMT is enabled, using %zu threads", nThreads);
    Ndmspc::NDimensionalExecutor executorMT(mins, maxs);
    // 1. Create the vector of NThreadData objects
    std::vector<Ndmspc::NHnSparseThreadData> threadDataVector(nThreads);
    for (size_t i = 0; i < threadDataVector.size(); ++i) {
      threadDataVector[i].SetAssignedIndex(i);
      threadDataVector[i].SetProcessFunc(func); // Set the processing function
      // thread_data_vector[i].GetHnstOutput(hnstOut);       // Set the input HnSparseTree
      // thread_data_vector[i].SetOutputGlobal(new TList()); // Set global output list
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
    Ndmspc::NLogger::Info("Merged %lld objects successfully", nmerged);
  }
  else {

    Ndmspc::NDimensionalExecutor executor(mins, maxs);

    TList * outputGlobal = new TList();
    auto    task         = [this, func, outputGlobal, binningDef](const std::vector<int> & coords) {
      Long64_t entry = 0;
      if (binningDef) {
        entry = binningDef->GetId(coords[0]); // Get the binning definition ID for the first axis
        Ndmspc::NLogger::Debug("Binning definition ID: %lld", entry);
        fBinning->GetContent()->GetBinContent(
            entry, fBinning->GetPoint()->GetCoords()); // Get the bin content for the given entry
      }

      TList * output = new TList();
      // output->SetOwner(true);                   // Set owner to delete objects in the list
      func(fBinning->GetPoint(), output, outputGlobal, 0); // Call the lambda function
      delete output;                                       // Clean up the output list
    };
    executor.Execute(task);
  }

  gROOT->SetBatch(batch); // Restore ROOT batch mode
  return true;
}
} // namespace Ndmspc
