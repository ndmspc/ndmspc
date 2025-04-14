#include <iostream>
#include "HnSparseIterator.h"
#include <omp.h>

int main()
{
  int num_threads = 1;
  // Define a 3-dimensional space from [0, 0, 0] to [10, 20, 5]
  // with 5, 10, and 5 steps in each dimension respectively
  std::vector<int> min_bounds = {0, 0, 0};
  std::vector<int> max_bounds = {10, 20, 5};
  std::vector<int> steps      = {5, 10, 5};

  // Create the iterator
  HnSparseIterator iterator(min_bounds, max_bounds, steps);

  // Print the total number of points
  std::cout << "Total points: " << iterator.getTotalPoints() << std::endl;

  // Define a thread-safe function to apply to each point
  auto processPoint = [](const std::vector<double> & point) {
    // Do some computation with the point
    double sum = 0.0;
    for (double val : point) {
      sum += val * val;
    }

// Use a critical section for thread-safe output
#pragma omp critical
    {
      std::cout << "Thread " << omp_get_thread_num() << " processed point (";
      for (size_t i = 0; i < point.size(); i++) {
        std::cout << point[i];
        if (i < point.size() - 1) std::cout << ", ";
      }
      std::cout << ") with result: " << sum << std::endl;
    }
  };

  // Process points in parallel (using 4 threads)
  std::cout << "Processing points in parallel with 4 threads..." << std::endl;
  iterator.forEachPointParallel(processPoint, num_threads);

  // Get and print a specific point
  std::vector<int>    indices = {2, 5, 3};
  std::vector<double> point   = iterator.getPoint(indices);

  std::cout << "Point at indices (2, 5, 3): (";
  for (size_t i = 0; i < point.size(); i++) {
    std::cout << point[i];
    if (i < point.size() - 1) std::cout << ", ";
  }
  std::cout << ")" << std::endl;

  return 0;
}
