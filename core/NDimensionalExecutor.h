#ifndef N_DIMENSIONAL_EXECUTOR_H
#define N_DIMENSIONAL_EXECUTOR_H

#include <vector>
#include <functional>
#include <cstddef>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <stdexcept>
#include <utility>
#include <exception>
#include <THnSparse.h>

namespace Ndmspc {

///
/// \class NDimensionalExecutor
/// \brief Executes a function over all points in an N-dimensional space, optionally in parallel.
/// \author Martin Vala <mvala@cern.ch>
///
class NDimensionalExecutor {
  public:
  /**
   * @brief Constructor from min/max bounds for each dimension.
   * @param minBounds Minimum bounds for each dimension.
   * @param maxBounds Maximum bounds for each dimension.
   */
  NDimensionalExecutor(const std::vector<int> & minBounds, const std::vector<int> & maxBounds);

  /**
   * @brief Constructor from THnSparse histogram.
   * @param hist Pointer to THnSparse histogram.
   * @param onlyfilled If true, iterate only filled bins.
   */
  NDimensionalExecutor(THnSparse * hist, bool onlyfilled = false);

  /**
   * @brief Execute a function over all coordinates in the N-dimensional space.
   * @param func Function to execute, taking coordinates as argument.
   */
  void Execute(const std::function<void(const std::vector<int> & coords)> & func);

  /**
   * @brief Execute a function in parallel over all coordinates, using thread-local objects.
   * @tparam TObject Type of thread-local object.
   * @param func Function to execute, taking coordinates and thread-local object.
   * @param thread_objects Vector of thread-local objects, one per thread.
   */
  template <typename TObject>
  void ExecuteParallel(const std::function<void(const std::vector<int> & coords, TObject & thread_object)> & func,
                       std::vector<TObject> & thread_objects);

  /**
   * @brief Returns the number of dimensions.
   * @return Number of dimensions.
   */
  size_t Dimensions() const { return fNumDimensions; }

  /**
   * @brief Returns the minimum bounds for each dimension.
   * @return Vector of minimum bounds.
   */
  const std::vector<int> & GetMinBounds() const { return fMinBounds; }

  /**
   * @brief Returns the maximum bounds for each dimension.
   * @return Vector of maximum bounds.
   */
  const std::vector<int> & GetMaxBounds() const { return fMaxBounds; }

  private:
  size_t           fNumDimensions; ///< Number of dimensions
  std::vector<int> fMinBounds;     ///< Minimum bounds for each dimension
  std::vector<int> fMaxBounds;     ///< Maximum bounds for each dimension
  std::vector<int> fCurrentCoords; ///< Current coordinates during iteration

  /**
   * @brief Increment the current coordinates to the next point in the N-dimensional space.
   * @return True if increment was successful, false if end reached.
   */
  bool Increment();
};

// --- Template Implementation for ExecuteParallel ---
/**
 * @brief Execute a function in parallel over all coordinates, using thread-local objects.
 *        Handles task distribution, synchronization, and exception propagation.
 *
 * @throws std::exception If any worker thread throws, the first exception is rethrown after joining.
 */
template <typename TObject>
void NDimensionalExecutor::ExecuteParallel(
    const std::function<void(const std::vector<int> & coords, TObject & thread_object)> & func,
    std::vector<TObject> &                                                                thread_objects)
{
  if (fNumDimensions == 0) {
    return;
  }
  size_t threads_to_use = thread_objects.size();
  if (threads_to_use == 0) {
    throw std::invalid_argument("Thread objects vector cannot be empty.");
  }

  std::vector<std::thread>                   workers;
  std::queue<std::function<void(TObject &)>> tasks;
  std::mutex                                 queue_mutex;
  std::condition_variable                    condition_producer;
  std::condition_variable                    condition_consumer;
  std::atomic<size_t>                        active_tasks = 0;
  std::atomic<bool>                          stop_pool    = false;
  // Optional: Store first exception encountered in workers
  std::exception_ptr first_exception = nullptr;
  std::mutex         exception_mutex;

  // Worker thread logic: fetch and execute tasks, handle exceptions, signal completion.
  auto worker_logic = [&](TObject & my_object) {
    while (true) {
      std::function<void(TObject &)> task_payload;
      bool                           task_acquired = false; // Track if we actually got a task this iteration

      try {
        { // Lock scope for queue access
          std::unique_lock<std::mutex> lock(queue_mutex);
          condition_producer.wait(lock, [&] { return stop_pool || !tasks.empty(); });

          // Check stop condition *after* waking up
          if (stop_pool && tasks.empty()) {
            break; // Exit the while loop normally
          }
          // If stopping but tasks remain, continue processing them

          // Only proceed if not stopping or if tasks are still present
          if (!tasks.empty()) {
            task_payload = std::move(tasks.front());
            tasks.pop();
            task_acquired = true; // We got a task
          }
          else {
            // Spurious wakeup or stop_pool=true with empty queue
            continue; // Go back to wait
          }
        } // Mutex unlocked

        // Execute the task if we acquired one
        if (task_acquired) {
          task_payload(my_object); // Execute task with assigned object
        }
      }
      catch (...) {
        // --- Exception Handling ---
        { // Lock to safely store the first exception
          std::lock_guard<std::mutex> lock(exception_mutex);
          if (!first_exception) {
            first_exception = std::current_exception(); // Store it
          }
        }
        // Signal pool to stop immediately on any error
        {
          std::unique_lock<std::mutex> lock(queue_mutex);
          stop_pool = true;
        }
        condition_producer.notify_all(); // Wake all threads to check stop flag

        // *** Crucial Fix: Decrement active_tasks even on exception ***
        // Check if we actually acquired a task before decrementing
        if (task_acquired) {
          if (--active_tasks == 0 && stop_pool) {
            // Also notify consumer here in case this was the last task
            condition_consumer.notify_one();
          }
        }
        // Decide whether to exit the worker or try processing remaining tasks
        // For simplicity, let's exit the worker on error.
        return; // Exit worker thread immediately on error
      }

      // --- Normal Task Completion ---
      // Decrement active task count *after* successful execution
      // Check if we actually acquired and processed a task
      if (task_acquired) {
        if (--active_tasks == 0 && stop_pool) {
          condition_consumer.notify_one();
        }
      }
    } // End of while loop
  }; // End of worker_logic lambda

  // --- Start Worker Threads ---
  workers.reserve(threads_to_use);
  for (size_t i = 0; i < threads_to_use; ++i) {
    workers.emplace_back(worker_logic, std::ref(thread_objects[i]));
  }

  // --- Main Thread: Iterate and Enqueue Tasks ---
  try {
    fCurrentCoords = fMinBounds;
    do {
      // Check if pool was stopped prematurely (e.g., by an exception in a worker)
      // Lock needed to safely check stop_pool
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop_pool) break;
      }

      std::vector<int> coords_copy = fCurrentCoords;
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // Double check stop_pool after acquiring lock
        if (stop_pool) break;

        active_tasks++;
        tasks.emplace([func, coords_copy](TObject & obj) { func(coords_copy, obj); });
      }
      condition_producer.notify_one();
    } while (Increment());
  }
  catch (...) {
    // Exception during iteration/enqueueing
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop_pool = true;       // Signal workers to stop
      if (!first_exception) { // Store exception if none from workers yet
        first_exception = std::current_exception();
      }
    }
    condition_producer.notify_all();
    // Proceed to join threads
  }

  // --- Signal Workers to Stop (if not already stopped by error) ---
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop_pool = true;
  }
  condition_producer.notify_all();

  // --- Wait for Tasks to Complete ---
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    condition_consumer.wait(lock, [&] { return stop_pool && active_tasks == 0; });
  }

  // --- Join Worker Threads ---
  for (std::thread & worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  // --- Check for and rethrow exception from workers ---
  if (first_exception) {
    std::rethrow_exception(first_exception);
  }
}

} // namespace Ndmspc

#endif
