#include <string>
#include <vector>
#include <THnSparse.h>
#include <TROOT.h>
#include <TH1.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
#include "NDimensionalExecutor.h"
#include "NHnSparseTreeThreadData.h"
#include "MyProcess.C"
// #include "PhiAnalysis.C"
void NHnSparseTreeProcessParallel(std::string filename        = "/tmp/hnst.root",
                                  std::string enabledBranches = "unlikepm,likepp")
{
  TH1::AddDirectory(kFALSE);

  // Optional: Suppress ROOT's info messages for cleaner output during thread execution
  gROOT->SetBatch(kTRUE);

  // Ndmspc::ProcessFuncPtr myfun = NdmspcUserProcess; // Use the function defined in MyProcess.C
  //
  std::vector<int>             min = {1};
  std::vector<int>             max = {400};
  Ndmspc::NDimensionalExecutor executor_par(min, max);

  // 1. Create the vector of NThreadData objects
  size_t num_threads_to_use = 0;

  // set num_threads_to_use to the number of available threads

  if (num_threads_to_use < 1) num_threads_to_use = std::thread::hardware_concurrency();
  Ndmspc::NLogger::Info("Using %zu threads\n", num_threads_to_use);
  std::vector<Ndmspc::NHnSparseTreeThreadData> thread_data_vector(num_threads_to_use);
  for (size_t i = 0; i < thread_data_vector.size(); ++i) {
    thread_data_vector[i].SetAssignedIndex(i);
    thread_data_vector[i].SetFileName(filename);
    thread_data_vector[i].SetEnabledBranches(enabledBranches);
    thread_data_vector[i].SetProcessFunc(NdmspcUserProcess); // Set the processing function
    // thread_data_vector[i].SetMacro(m);
    // thread_data_vector[i].GetHnSparseTree(); // Initialize the NHnSparseTree
  }

  auto start_par = std::chrono::high_resolution_clock::now();

  // 2. Define the lambda (type in signature updated)
  auto parallel_task = [](const std::vector<int> & coords, Ndmspc::NHnSparseTreeThreadData & thread_obj) {
    thread_obj.Print();
    thread_obj.Process(coords);
  };

  // 3. Call ExecuteParallel (template argument updated)
  // Use renamed class NThreadData
  executor_par.ExecuteParallel<Ndmspc::NHnSparseTreeThreadData>(parallel_task, thread_data_vector);
  auto                                      end_par      = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> par_duration = end_par - start_par;

  Ndmspc::NLogger::Info("Parallel execution completed and it took %.3f s", par_duration.count() / 100);

  // --- Process Results (Inspect the original vector) ---
  // Merging results
  std::cout << "Results from " << thread_data_vector.size() << " provided TObjects:" << std::endl;
  long long total_items = 0;
  long long total_sum   = 0;
  for (const auto & data : thread_data_vector) {
    data.Print();
    total_items += data.GetItemCount();
    total_sum += data.GetCoordSum();
  }
  std::cout << "Total items processed: " << total_items << std::endl;
  std::cout << "Total coordinate sum: " << total_sum << std::endl;
}
