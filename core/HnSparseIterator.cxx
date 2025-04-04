#include "HnSparseIterator.h"
#include <stdexcept>
#include <omp.h>

// Helper method for recursive iteration
void HnSparseIterator::iterateRecursively(
    std::vector<double>& current_point,
    int dimension,
    const std::function<void(const std::vector<double>&)>& function
) {
    // Base case: if we've set values for all dimensions, apply the function
    if (dimension == dimensions) {
        function(current_point);
        return;
    }

    // Recursive case: iterate through all values in the current dimension
    for (int i = 0; i <= steps[dimension]; i++) {
        // Calculate the actual coordinate value
        current_point[dimension] = min_bounds[dimension] + i * step_sizes[dimension];

        iterateRecursively(current_point, dimension + 1, function);
    }
}

// Constructor
HnSparseIterator::HnSparseIterator(
    const std::vector<int>& min_bounds,
    const std::vector<int>& max_bounds,
    const std::vector<int>& steps
) : min_bounds(min_bounds), max_bounds(max_bounds), steps(steps) {
    // Validate input
    if (min_bounds.size() != max_bounds.size() || min_bounds.size() != steps.size()) {
        throw std::invalid_argument("Dimension mismatch between bounds and steps");
    }

    dimensions = min_bounds.size();
    step_sizes.resize(dimensions);

    for (int i = 0; i < dimensions; i++) {
        if (min_bounds[i] > max_bounds[i]) {
            throw std::invalid_argument("Min bound greater than max bound");
        }
        if (steps[i] < 1) {
            throw std::invalid_argument("Steps must be at least 1");
        }

        // Calculate step sizes
        step_sizes[i] = static_cast<double>(max_bounds[i] - min_bounds[i]) / steps[i];
    }
}

// Method to iterate through all points recursively
void HnSparseIterator::forEachPoint(const std::function<void(const std::vector<double>&)>& function) {
    std::vector<double> current_point(dimensions);
    iterateRecursively(current_point, 0, function);
}

// Method to iterate through all points non-recursively (more efficient for large spaces)
void HnSparseIterator::forEachPointNonRecursive(const std::function<void(const std::vector<double>&)>& function) {
    std::vector<double> point(dimensions);
    std::vector<int> indices(dimensions, 0);

    // Initialize the first point
    for (int i = 0; i < dimensions; i++) {
        point[i] = min_bounds[i];
    }

    // Process the first point
    function(point);

    // Iterate through all points
    bool done = false;
    while (!done) {
        // Find the rightmost dimension that can be incremented
        int dim = dimensions - 1;
        while (dim >= 0) {
            indices[dim]++;
            if (indices[dim] <= steps[dim]) {
                break;
            }
            indices[dim] = 0;
            dim--;
        }

        // If we couldn't increment any dimension, we're done
        if (dim < 0) {
            done = true;
            continue;
        }

        // Update the point coordinates
        for (int i = 0; i < dimensions; i++) {
            point[i] = min_bounds[i] + indices[i] * step_sizes[i];
        }

        // Process the point
        function(point);
    }
}

// Method to iterate through all points in parallel using OpenMP
void HnSparseIterator::forEachPointParallel(
    const std::function<void(const std::vector<double>&)>& function,
    int num_threads
) {
    // Set number of threads if specified
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }

    // Calculate total number of points
    size_t total_points = getTotalPoints();

    // Use OpenMP to parallelize the loop
    #pragma omp parallel
    {
        // Each thread gets its own copy of point
        std::vector<double> point(dimensions);
        std::vector<int> indices(dimensions);

        // Get thread info
        int thread_id = omp_get_thread_num();
        int total_threads = omp_get_num_threads();

        // Calculate chunk size for each thread
        size_t chunk_size = total_points / total_threads;
        size_t start_idx = thread_id * chunk_size;
        size_t end_idx = (thread_id == total_threads - 1) ?
                         total_points : (thread_id + 1) * chunk_size;

        // Skip to the starting index for this thread
        if (start_idx > 0) {
            size_t idx = start_idx;
            for (int d = dimensions - 1; d >= 0; d--) {
                int dim_size = steps[d] + 1;
                indices[d] = idx % dim_size;
                idx /= dim_size;
            }

            // Set the initial point for this thread
            for (int d = 0; d < dimensions; d++) {
                point[d] = min_bounds[d] + indices[d] * step_sizes[d];
            }
        } else {
            // First thread starts at the beginning
            for (int d = 0; d < dimensions; d++) {
                indices[d] = 0;
                point[d] = min_bounds[d];
            }
        }

        // Process the first point for this thread
        function(point);

        // Process remaining points for this thread
        for (size_t i = start_idx + 1; i < end_idx; i++) {
            // Find the rightmost dimension that can be incremented
            int dim = dimensions - 1;
            while (dim >= 0) {
                indices[dim]++;
                if (indices[dim] <= steps[dim]) {
                    break;
                }
                indices[dim] = 0;
                dim--;
            }

            // Update the point coordinates
            for (int d = 0; d < dimensions; d++) {
                point[d] = min_bounds[d] + indices[d] * step_sizes[d];
            }

            // Process the point
            function(point);
        }
    }
}

// Method to iterate through all points in parallel using OpenMP with a thread-safe function
void HnSparseIterator::forEachPointParallelSimple(
    const std::function<void(const std::vector<double>&)>& function,
    int num_threads
) {
    // Set number of threads if specified
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }

    // Calculate total number of points
    size_t total_points = getTotalPoints();

    // Use OpenMP to parallelize the loop
    #pragma omp parallel for
    for (size_t idx = 0; idx < total_points; idx++) {
        // Each thread gets its own copy of point and indices
        std::vector<double> point(dimensions);
        std::vector<int> indices(dimensions, 0);

        // Convert linear index to n-dimensional indices
        size_t temp_idx = idx;
        for (int d = dimensions - 1; d >= 0; d--) {
            int dim_size = steps[d] + 1;
            indices[d] = temp_idx % dim_size;
            temp_idx /= dim_size;
        }

        // Calculate the point coordinates
        for (int d = 0; d < dimensions; d++) {
            point[d] = min_bounds[d] + indices[d] * step_sizes[d];
        }

        // Process the point
        function(point);
    }
}

// Get the total number of points in the space
size_t HnSparseIterator::getTotalPoints() const {
    size_t total = 1;
    for (int i = 0; i < dimensions; i++) {
        total *= (steps[i] + 1);
    }
    return total;
}

// Get the dimensions of the space
int HnSparseIterator::getDimensions() const {
    return dimensions;
}

// Get a specific point by its indices
std::vector<double> HnSparseIterator::getPoint(const std::vector<int>& indices) const {
    if (indices.size() != dimensions) {
        throw std::invalid_argument("Indices dimension mismatch");
    }

    std::vector<double> point(dimensions);
    for (int i = 0; i < dimensions; i++) {
        if (indices[i] < 0 || indices[i] > steps[i]) {
            throw std::out_of_range("Index out of range");
        }
        point[i] = min_bounds[i] + indices[i] * step_sizes[i];
    }

    return point;
}

