#ifndef N_DIMENSIONAL_EXECUTOR_H
#define N_DIMENSIONAL_EXECUTOR_H

#include <iomanip>
#include <sstream>
#include <set>
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
#include "NTaskStateManager.h"

namespace Ndmspc {

/// @brief Execution progress metrics for IPC-based distributed processing
struct ExecutionProgress {
  size_t tasksAcked{0};     ///< Tasks completed and ACKed by workers
  size_t tasksPending{0};   ///< Tasks waiting to be dispatched
  size_t tasksRunning{0};   ///< Tasks currently assigned to workers  
  size_t tasksDone{0};      ///< Tasks completed locally (state machine)
  size_t activeWorkers{0};  ///< Number of workers currently active
};

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
  void   StartProcessIpc(std::vector<NThreadData *> & workerObjects, size_t processCount,
                         const std::string & tcpBindEndpoint = "", const std::string & jobDir = "",
                         const std::string & treeName = "ngnt", const std::string & macroList = "",
                         const std::string & tmpDir = "", const std::string & tmpResultsDir = "",
                         const std::string & macroParams = "");
  size_t ExecuteCurrentBoundsProcessIpc(const std::string & definitionName = "",
                                        const std::vector<Long64_t> * definitionIds = nullptr,
                                        const std::function<void(const ExecutionProgress&)> & progressCallback = nullptr);
  void   FinishProcessIpc(bool abort = false);

  std::set<size_t> GetRegisteredWorkerIndices() const { return fRegisteredWorkerIndices; }

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

  /// Sends INIT to a newly-connected TCP worker, waits for ACK, and registers
  /// it in identityToWorker / workerIdentityVec.
  bool InitTcpWorker(const std::string & identity);

  /// Handles a BOOTSTRAP message from a worker: assigns the next sequential
  /// index and replies with a CONFIG frame containing macro list, macro params,
  /// and env vars.
  bool HandleBootstrap(const std::string & identity);

  /// Centralized worker failure handling: recovers tasks, removes worker, updates state.
  /// Returns the count of tasks redistributed to the pending queue.
  size_t HandleWorkerFailure(const std::string & failedIdentity,
                             const std::string & failureReason,
                             size_t & outstanding,
                             size_t & acked);

  struct IpcSession;
  std::unique_ptr<IpcSession> fIpcSession;
  std::set<size_t>            fRegisteredWorkerIndices; ///< Worker indices that completed registration (TCP mode)
};



} // namespace Ndmspc

#endif
