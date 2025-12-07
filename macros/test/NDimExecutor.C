#include <iostream>
#include <vector>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <mutex>
#include "NLogger.h"
#include "NDimensionalExecutor.h"
#include "NThreadData.h"
using namespace Ndmspc;

// Mutex to protect cout
std::mutex cout_mutex;

// Function to print coordinates safely
void print_coordinates_safe(const std::vector<int> & coords, size_t index)
{
  std::lock_guard<std::mutex> lock(cout_mutex);
  std::cout << "[Obj " << index << " / Thr " << std::this_thread::get_id() << "] (";
  for (size_t i = 0; i < coords.size(); ++i) {
    std::cout << coords[i] << (i == coords.size() - 1 ? "" : ", ");
  }
  std::cout << ")" << std::endl;
}

int NDimExecutor()
{
  try {
    // --- Sequential Example ---
    std::vector<int> min2D = {0, 0};
    std::vector<int> max2D = {10, 20};
    // std::vector<int> max2D = {20000, 20000};
    NLogInfo("2D Example: (%d, %d) to (%d, %d)", min2D[0], min2D[1], max2D[0], max2D[1]);
    NDimensionalExecutor exec2D_seq(min2D, max2D);
    // exec2D_seq.Execute([](const std::vector<int> & c) { print_coordinates_safe(c, c.size()); });
    exec2D_seq.Execute([](const std::vector<int> & c) {});
    NLogInfo("Sequential 2D execution completed.");
    // --- Parallel Example with User-Provided TObjects ---
    std::vector<int> min3D = {0, 0, 0};
    std::vector<int> max3D = {500, 60, 70};
    NLogInfo("3D Example: (%d, %d, %d) to (%d, %d, %d)", min3D[0], min3D[1], min3D[2], max3D[0], max3D[1],
                  max3D[2]);
    NDimensionalExecutor exec3D_par(min3D, max3D);

    // 1. Create the vector of NThreadData objects
    size_t num_threads_to_use = 4;

    // set num_threads_to_use to the number of available threads
    num_threads_to_use = std::thread::hardware_concurrency();
    printf("Using %zu threads\n", num_threads_to_use);
    std::vector<NThreadData> thread_data_vector(num_threads_to_use);
    for (size_t i = 0; i < thread_data_vector.size(); ++i) {
      thread_data_vector[i].SetAssignedIndex(i);
    }

    auto start_par3d = std::chrono::high_resolution_clock::now();

    // 2. Define the lambda (type in signature updated)
    auto parallel_task = [](const std::vector<int> & coords, NThreadData & thread_obj) {
      thread_obj.Process(coords);
      // thread_obj.Print();
    };

    // 3. Call ExecuteParallel (template argument updated)
    // Use renamed class NThreadData
    exec3D_par.ExecuteParallel<NThreadData>(parallel_task, thread_data_vector);

    auto                                      end_par3d      = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> par3d_duration = end_par3d - start_par3d;

    NLogInfo("Parallel 3D execution completed and it took %.0f ms", par3d_duration.count());

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

    // Verification (Unchanged)
    long long expected_sum   = 0;
    long long expected_count = 0;
    exec3D_par.Execute([&](const std::vector<int> & coords) {
      expected_count++;
      for (int v : coords) expected_sum += v;
    });
    std::cout << "Expected items (sequential): " << expected_count << std::endl;
    std::cout << "Expected sum (sequential): " << expected_sum << std::endl;
    if (total_items == expected_count && total_sum == expected_sum) {
      std::cout << "Verification PASSED!" << std::endl;
    }
    else {
      std::cout << "Verification FAILED!" << std::endl;
    }
    std::cout << "-------------------------------------\n" << std::endl;

    // --- Error Case ---
    std::cout << "--- Error Example: Min > Max ---" << std::endl;
    try {
      std::vector<int>     min_err = {1, 5};
      std::vector<int>     max_err = {3, 4};
      NDimensionalExecutor exec_err(min_err, max_err);
      exec_err.Execute([](const std::vector<int> & c) { /* do nothing */ });
    }
    catch (const std::invalid_argument & e) {
      std::cerr << "Caught expected exception: " << e.what() << std::endl;
    }
    std::cout << "-------------------------------------\n" << std::endl;
  }
  catch (const std::invalid_argument & e) {
    std::cerr << "Error creating executor: " << e.what() << std::endl;
    return 1;
  }
  catch (const std::exception & e) {
    std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
