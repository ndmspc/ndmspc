#ifndef NdmspcNActionWorker_H
#define NdmspcNActionWorker_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace Ndmspc {

/**
 * @class NActionWorker
 * @brief The one thread tool actions run on.
 *
 * The HTTP server processes requests on a single ROOT thread, and running an action there blocks it -
 * so a cancel frame could never be read while the action runs. Actions are therefore run here, on one
 * dedicated thread: the ROOT thread submits and returns, and stays free to read the next frame (the
 * cancel). One thread, not a pool, keeps Cling/ROOT single-threaded exactly as before.
 */
class NActionWorker {
  public:
  NActionWorker();
  ~NActionWorker();
  NActionWorker(const NActionWorker &) = delete;
  NActionWorker & operator=(const NActionWorker &) = delete;

  /// @brief Queue a job to run on the worker thread.
  void Submit(std::function<void()> job);
  /// @brief Whether the calling thread is the worker thread (used to run re-entrant work inline).
  bool OnWorkerThread() const;

  private:
  void Loop();

  std::thread                        fThread;      ///< The worker thread
  std::thread::id                    fId;          ///< Its id, for OnWorkerThread
  std::mutex                         fMutex;       ///< Guards the queue/stop flag
  std::condition_variable            fCv;          ///< Wakes the worker
  std::queue<std::function<void()>>  fJobs;        ///< Pending jobs
  bool                               fStopping{false}; ///< Stop requested; pending jobs are dropped
};

} // namespace Ndmspc
#endif
