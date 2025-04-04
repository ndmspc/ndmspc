#ifndef HN_SPARSE_ITERATOR_H
#define HN_SPARSE_ITERATOR_H

#include <vector>
#include <functional>

class HnSparseIterator {
private:
    std::vector<int> min_bounds;
    std::vector<int> max_bounds;
    std::vector<int> steps;
    std::vector<double> step_sizes;
    int dimensions;

    // Helper method for recursive iteration
    void iterateRecursively(
        std::vector<double>& current_point,
        int dimension,
        const std::function<void(const std::vector<double>&)>& function
    );

public:
    // Constructor
    HnSparseIterator(
        const std::vector<int>& min_bounds,
        const std::vector<int>& max_bounds,
        const std::vector<int>& steps
    );

    // Method to iterate through all points recursively
    void forEachPoint(const std::function<void(const std::vector<double>&)>& function);

    // Method to iterate through all points non-recursively (more efficient for large spaces)
    void forEachPointNonRecursive(const std::function<void(const std::vector<double>&)>& function);

    // Method to iterate through all points in parallel using OpenMP
    void forEachPointParallel(
        const std::function<void(const std::vector<double>&)>& function,
        int num_threads = 0
    );

    // Method to iterate through all points in parallel using OpenMP with a thread-safe function
    // This version is simpler but may have load balancing issues for large spaces
    void forEachPointParallelSimple(
        const std::function<void(const std::vector<double>&)>& function,
        int num_threads = 0
    );

    // Get the total number of points in the space
    size_t getTotalPoints() const;

    // Get the dimensions of the space
    int getDimensions() const;

    // Get a specific point by its indices
    std::vector<double> getPoint(const std::vector<int>& indices) const;
};

#endif // HN_SPARSE_ITERATOR_H

