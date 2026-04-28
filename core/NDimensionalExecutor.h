#ifndef N_DIMENSIONAL_EXECUTOR_H
#define N_DIMENSIONAL_EXECUTOR_H

#include <iomanip>
#include <sstream>
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
#include <memory>
#include <string>
#include <Rtypes.h>
#include <THnSparse.h>
#include "NLogger.h"
#include "NThreadData.h"

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

  ~NDimensionalExecutor();

  void SetBounds(const std::vector<int> & minBounds, const std::vector<int> & maxBounds);

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
   * @brief Execute fixed-contract processing in multiple child processes over IPC.
   * @param workerObjects Worker objects (NThreadData-derived) used by child processes.
   * @param processCount Number of worker processes to use.
   * @return Number of processed tasks acknowledged by workers.
   */
  size_t ExecuteParallelProcessIpc(std::vector<NThreadData *> & workerObjects, size_t processCount);
  void   StartProcessIpc(std::vector<NThreadData *> & workerObjects, size_t processCount);
  size_t ExecuteCurrentBoundsProcessIpc(const std::string & definitionName = "",
                                        const std::vector<Long64_t> * definitionIds = nullptr,
                                        const std::function<void(size_t, size_t)> & progressCallback = nullptr);
  void   FinishProcessIpc();

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

  struct IpcSession;
  std::unique_ptr<IpcSession> fIpcSession;
};



} // namespace Ndmspc

#endif
