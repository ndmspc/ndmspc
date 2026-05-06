#include <string>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <sys/wait.h>
#include <unistd.h>
#include <zmq.h>
#include <THnSparse.h>
#include <TAxis.h>
#include <TROOT.h>
#include <TSystem.h>
#include "NDimensionalExecutor.h"
#include "NDimensionalIpcRunner.h"
#include "NGnThreadData.h"
#include "NUtils.h"

namespace Ndmspc {

namespace {
volatile sig_atomic_t gIpcSigIntRequested = 0;
volatile sig_atomic_t gIpcChildCount       = 0;
pid_t                 gIpcChildPids[1024]  = {0};

void IpcSigIntHandler(int)
{
  gIpcSigIntRequested = 1;
  const sig_atomic_t count = gIpcChildCount;
  for (sig_atomic_t i = 0; i < count; ++i) {
    if (gIpcChildPids[i] > 0) {
      // User interrupt should not leave orphan IPC workers.
      kill(gIpcChildPids[i], SIGKILL);
    }
  }
}

void InstallIpcSigIntHandler(const std::vector<pid_t> & childPids, struct sigaction & oldAction, bool & hasOldAction)
{
  gIpcSigIntRequested = 0;
  gIpcChildCount      = 0;
  const size_t maxChildren = sizeof(gIpcChildPids) / sizeof(gIpcChildPids[0]);
  const size_t count       = std::min(maxChildren, childPids.size());
  for (size_t i = 0; i < count; ++i) {
    gIpcChildPids[i] = childPids[i];
  }
  for (size_t i = count; i < maxChildren; ++i) {
    gIpcChildPids[i] = 0;
  }
  gIpcChildCount = static_cast<sig_atomic_t>(count);

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = IpcSigIntHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, &oldAction) == 0) {
    hasOldAction = true;
  }
}

void RestoreIpcSigIntHandler(const struct sigaction & oldAction, bool hasOldAction)
{
  if (hasOldAction) {
    sigaction(SIGINT, &oldAction, nullptr);
  }
  gIpcChildCount      = 0;
  gIpcSigIntRequested = 0;
}
} // namespace

struct NDimensionalExecutor::IpcSession {
  void * ctx{nullptr};
  void * router{nullptr};
  bool   isTcp{false};
  std::string endpointPath;
  std::string endpoint;
  std::vector<pid_t> childPids;
  std::unordered_map<std::string, size_t> identityToWorker;
  std::vector<std::string> workerIdentityVec; // ordered list for round-robin
  // TCP late-joiner support: stored so new workers can be initialised mid-run
  std::string                jobDir;
  std::string                treeName;
  std::vector<NThreadData *> * workerObjects{nullptr};
  size_t                     maxWorkers{0};
  std::string                currentDefName;
  std::vector<Long64_t>      currentDefIds;
  bool                       hasCurrentDefIds{false};
  struct sigaction oldSigIntAction{};
  bool             hasOldSigIntAction{false};
  // Bootstrap configuration sent to workers on first contact
  std::string macroList;       // comma-separated macro paths to load on worker
  std::string macroParams;     // parameter list forwarded to TMacro::Exec on worker
  std::string tmpDir;          // supervisor's NDMSPC_TMP_DIR (fallback for workers)
  std::string tmpResultsDir;   // supervisor's NDMSPC_TMP_RESULTS_DIR
  size_t      bootstrapNextIdx{0}; // auto-assigned index counter for BOOTSTRAP
  std::unordered_map<std::string, size_t> bootstrapAssignments; // BOOTSTRAP identity -> assigned slot
  std::vector<std::string> pendingReadyIdentities; // READY messages consumed while waiting for ACK
  // Task state management: unified handling of pending, running, and done tasks
  NTaskStateManager taskStateManager;
  std::unordered_map<std::string, std::set<size_t>> workerTaskHistory; // tasks assigned to each worker in current definition
  std::set<std::string> earlyDoneWorkers; // workers that sent DONE before FinishProcessIpc started waiting
  // TCP worker activity tracking for failure detection
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> workerLastActivity; // identity -> last ACK time
  std::set<std::string> failedTcpWorkers; // identities of TCP workers that have failed
};

// --- Private Increment Logic ---
bool NDimensionalExecutor::Increment()
{
  for (int i = fNumDimensions - 1; i >= 0; --i) {
    fCurrentCoords[i]++;
    if (fCurrentCoords[i] <= fMaxBounds[i]) {
      return true;
    }
    fCurrentCoords[i] = fMinBounds[i];
  }
  return false;
}

NDimensionalExecutor::NDimensionalExecutor(const std::vector<int> & minBounds, const std::vector<int> & maxBounds)
    : fMinBounds(minBounds), fMaxBounds(maxBounds)
{
  ///
  /// Constructor
  ///

  if (fMinBounds.size() != fMaxBounds.size()) {
    throw std::invalid_argument("Min and max bounds vectors must have the same size.");
  }
  if (fMinBounds.empty()) {
    throw std::invalid_argument("Bounds vectors cannot be empty.");
  }

  fNumDimensions = fMinBounds.size();

  for (size_t i = 0; i < fNumDimensions; ++i) {
    if (fMinBounds[i] > fMaxBounds[i]) {
      throw std::invalid_argument("Min bound (" + std::to_string(fMinBounds[i]) +
                                  ") cannot be greater than max bound (" + std::to_string(fMaxBounds[i]) +
                                  ") for dimension " + std::to_string(i));
    }
  }

  fCurrentCoords.resize(fNumDimensions);
  fCurrentCoords = fMinBounds;
}

void NDimensionalExecutor::SetBounds(const std::vector<int> & minBounds, const std::vector<int> & maxBounds)
{
  fMinBounds = minBounds;
  fMaxBounds = maxBounds;

  if (fMinBounds.size() != fMaxBounds.size()) {
    throw std::invalid_argument("Min and max bounds vectors must have the same size.");
  }
  if (fMinBounds.empty()) {
    throw std::invalid_argument("Bounds vectors cannot be empty.");
  }

  fNumDimensions = fMinBounds.size();
  for (size_t i = 0; i < fNumDimensions; ++i) {
    if (fMinBounds[i] > fMaxBounds[i]) {
      throw std::invalid_argument("Min bound (" + std::to_string(fMinBounds[i]) +
                                  ") cannot be greater than max bound (" + std::to_string(fMaxBounds[i]) +
                                  ") for dimension " + std::to_string(i));
    }
  }

  fCurrentCoords.resize(fNumDimensions);
  fCurrentCoords = fMinBounds;
}

NDimensionalExecutor::NDimensionalExecutor(THnSparse * hist, bool onlyfilled)
{
  ///
  /// Constructor with THnSparse input
  ///
  if (hist == nullptr) {
    throw std::invalid_argument("THnSparse pointer cannot be null.");
  }

  if (onlyfilled) {
    // Check if the histogram is filled
    if (hist->GetNbins() <= 0) {
      throw std::invalid_argument("THnSparse histogram is empty.");
    }

    fMinBounds.push_back(0);
    fMaxBounds.push_back(hist->GetNbins());
  }
  else {
    // loop over all dimensions
    for (int i = 0; i < hist->GetNdimensions(); ++i) {
      fMinBounds.push_back(0);
      fMaxBounds.push_back(hist->GetAxis(i)->GetNbins());
    }
  }
  fNumDimensions = fMinBounds.size();
  fCurrentCoords.resize(fNumDimensions);
  fCurrentCoords = fMinBounds;
}

NDimensionalExecutor::~NDimensionalExecutor() = default;

void NDimensionalExecutor::Execute(const std::function<void(const std::vector<int> & coords)> & func)
{
  ///
  /// Sequential Execution
  ///

  if (fNumDimensions == 0) {
    return;
  }
  fCurrentCoords = fMinBounds; // Reset state
  do {
    func(fCurrentCoords);
  } while (Increment());
}

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
    NThreadData * md = (NThreadData *)&my_object;

    std::ostringstream oss;
    oss << "wk_" << std::setw(6) << std::setfill('0') << md->GetAssignedIndex();

    NLogger::SetThreadName(oss.str());
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

          // Drain not-yet-executed tasks from the queue and decrement their
          // active counter contribution. Without this, active_tasks can remain
          // non-zero forever and the consumer wait below deadlocks.
          size_t dropped = 0;
          while (!tasks.empty()) {
            tasks.pop();
            ++dropped;
          }
          if (dropped > 0) {
            active_tasks.fetch_sub(dropped);
          }
        }
        condition_producer.notify_all(); // Wake all threads to check stop flag
        condition_consumer.notify_one(); // Wake consumer in case queue drain reached zero

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

size_t NDimensionalExecutor::ExecuteParallelProcessIpc(std::vector<NThreadData *> & workerObjects,
                                                       size_t                       processCount)
{
  StartProcessIpc(workerObjects, processCount);
  try {
    size_t acked = ExecuteCurrentBoundsProcessIpc();
    FinishProcessIpc();
    return acked;
  }
  catch (...) {
    FinishProcessIpc();
    throw;
  }
}

bool NDimensionalExecutor::InitTcpWorker(const std::string & identity)
{
  // Extract worker index from identity string (e.g. "wk_001")
  // Derive the prefix by stripping trailing digits from a sample identity
  const std::string sample   = NDimensionalIpcRunner::BuildWorkerIdentity(0);
  size_t            numLen   = 0;
  while (numLen < sample.size() && std::isdigit((unsigned char)sample[sample.size() - 1 - numLen]))
    ++numLen;
  const size_t      prefixLen = sample.size() - numLen; // e.g. strlen("wk_") == 3
  if (identity.size() <= prefixLen) return false;
  size_t workerIdx = 0;
  try {
    workerIdx = std::stoul(identity.substr(prefixLen));
  }
  catch (...) {
    NLogWarning("NDimensionalExecutor::InitTcpWorker: cannot parse index from identity '%s'", identity.c_str());
    return false;
  }
  if (workerIdx >= fIpcSession->maxWorkers) {
    NLogWarning("NDimensionalExecutor::InitTcpWorker: worker index %zu >= maxWorkers %zu, ignoring",
                workerIdx, fIpcSession->maxWorkers);
    return false;
  }
  if (fIpcSession->identityToWorker.count(identity)) {
    NLogWarning("NDimensionalExecutor::InitTcpWorker: worker '%s' already registered, ignoring duplicate READY",
                identity.c_str());
    return false;
  }

  const std::string sessionId = std::to_string(getpid());
  if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router,
                                         {identity, "INIT", std::to_string(workerIdx), sessionId,
                                          fIpcSession->jobDir, fIpcSession->treeName,
                                          fIpcSession->tmpDir, fIpcSession->tmpResultsDir})) {
    NLogError("NDimensionalExecutor::InitTcpWorker: failed to send INIT to '%s'", identity.c_str());
    return false;
  }

  int initTimeoutSec = 30;
  if (const char * env = gSystem->Getenv("NDMSPC_WORKER_TIMEOUT")) {
    try {
      initTimeoutSec = std::max(1, std::stoi(env));
    }
    catch (...) {
      NLogWarning("NDimensionalExecutor::InitTcpWorker: Invalid NDMSPC_WORKER_TIMEOUT='%s', using default=%d", env,
                  initTimeoutSec);
    }
  }
  const auto initDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(initTimeoutSec);
  bool       acked        = false;
  while (!acked) {
    std::vector<std::string> ackFrames;
    if (!NDimensionalIpcRunner::ReceiveFrames(fIpcSession->router, ackFrames)) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (std::chrono::steady_clock::now() > initDeadline) break;
        continue;
      }
      break;
    }
    if (ackFrames.size() >= 2 && ackFrames[1] == "BOOTSTRAP") {
      HandleBootstrap(ackFrames[0]);
      continue;
    }
    if (ackFrames.size() >= 2 && ackFrames[1] == "READY" && ackFrames[0] != identity) {
      if (!fIpcSession->identityToWorker.count(ackFrames[0]) &&
          std::find(fIpcSession->pendingReadyIdentities.begin(), fIpcSession->pendingReadyIdentities.end(),
                    ackFrames[0]) == fIpcSession->pendingReadyIdentities.end()) {
        fIpcSession->pendingReadyIdentities.push_back(ackFrames[0]);
      }
      continue;
    }
    if (ackFrames.size() >= 2 && ackFrames[0] == identity && ackFrames[1] == "ACK") {
      acked = true;
    }
  }
  if (!acked) {
    NLogError("NDimensionalExecutor::InitTcpWorker: worker '%s' did not ACK INIT", identity.c_str());
    return false;
  }

  // Check for duplicate identity (defensive - shouldn't happen with proper cleanup)
  if (fIpcSession->identityToWorker.count(identity)) {
    NLogWarning("NDimensionalExecutor::InitTcpWorker: worker '%s' already registered, replacing", identity.c_str());
    // Remove from vector if present
    auto it = std::find(fIpcSession->workerIdentityVec.begin(), fIpcSession->workerIdentityVec.end(), identity);
    if (it != fIpcSession->workerIdentityVec.end()) {
      fIpcSession->workerIdentityVec.erase(it);
    }
  }

  fIpcSession->identityToWorker[identity] = workerIdx;
  fIpcSession->workerIdentityVec.push_back(identity);
  // Initialize activity tracking for TCP worker
  fIpcSession->workerLastActivity[identity] = std::chrono::steady_clock::now();
  NLogInfo("NDimensionalExecutor::InitTcpWorker: worker '%s' (idx=%zu) joined [total: %zu]",
           identity.c_str(), workerIdx, fIpcSession->workerIdentityVec.size());
  return true;
}

bool NDimensionalExecutor::HandleBootstrap(const std::string & identity)
{
  if (!fIpcSession || !fIpcSession->isTcp) return false;

  // Repeat BOOTSTRAP from the same identity should be idempotent.
  auto existing = fIpcSession->bootstrapAssignments.find(identity);
  if (existing != fIpcSession->bootstrapAssignments.end()) {
    const size_t assignedIdx = existing->second;
    return NDimensionalIpcRunner::SendFrames(fIpcSession->router,
                                             {identity, "CONFIG", std::to_string(assignedIdx),
                                              fIpcSession->macroList, fIpcSession->tmpDir,
                                              fIpcSession->tmpResultsDir,
                                              fIpcSession->macroParams});
  }

  if (fIpcSession->bootstrapNextIdx >= fIpcSession->maxWorkers) {
    NLogWarning("NDimensionalExecutor::HandleBootstrap: rejecting worker '%s' (capacity reached: %zu)",
                identity.c_str(), fIpcSession->maxWorkers);
    return NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "REJECT", "capacity"});
  }

  const size_t assignedIdx = fIpcSession->bootstrapNextIdx++;

  fIpcSession->bootstrapAssignments[identity] = assignedIdx;

  NLogDebug("NDimensionalExecutor::HandleBootstrap: assigning index %zu to worker '%s'", assignedIdx,
            identity.c_str());
  return NDimensionalIpcRunner::SendFrames(fIpcSession->router,
                                           {identity, "CONFIG", std::to_string(assignedIdx),
                                            fIpcSession->macroList, fIpcSession->tmpDir,
                                            fIpcSession->tmpResultsDir,
                                            fIpcSession->macroParams});
}

void NDimensionalExecutor::StartProcessIpc(std::vector<NThreadData *> & workerObjects, size_t processCount,
                                           const std::string & tcpBindEndpoint, const std::string & jobDir,
                                           const std::string & treeName, const std::string & macroList,
                                           const std::string & tmpDir, const std::string & tmpResultsDir,
                                           const std::string & macroParams)
{
  if (workerObjects.empty()) {
    throw std::invalid_argument("Worker objects vector cannot be empty.");
  }
  if (fIpcSession) {
    throw std::runtime_error("IPC session is already active.");
  }

  const size_t processesToUse = std::max<size_t>(1, std::min(processCount, workerObjects.size()));
  NLogInfo("NDimensionalExecutor::StartProcessIpc: requested=%zu, workerObjects=%zu, spawning=%zu", processCount,
           workerObjects.size(), processesToUse);
  const auto   nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::high_resolution_clock::now().time_since_epoch())
                     .count();
  fIpcSession = std::make_unique<IpcSession>();

  const bool isTcp = !tcpBindEndpoint.empty();
  fIpcSession->isTcp = isTcp;

  if (isTcp) {
    fIpcSession->endpoint = tcpBindEndpoint;
    fIpcSession->endpointPath.clear();
  } else {
    fIpcSession->endpointPath = "/tmp/ndmspc_ipc_" + std::to_string(getpid()) + "_" + std::to_string(nowNs) + ".sock";
    fIpcSession->endpoint = "ipc://" + fIpcSession->endpointPath;
    ::unlink(fIpcSession->endpointPath.c_str());
  }

  fIpcSession->ctx = zmq_ctx_new();
  if (!fIpcSession->ctx) {
    fIpcSession.reset();
    throw std::runtime_error("Failed to create ZeroMQ context.");
  }

  fIpcSession->router = zmq_socket(fIpcSession->ctx, ZMQ_ROUTER);
  if (!fIpcSession->router) {
    zmq_ctx_term(fIpcSession->ctx);
    fIpcSession.reset();
    throw std::runtime_error("Failed to create ZeroMQ ROUTER socket.");
  }

  int timeoutMs = 1000;
  zmq_setsockopt(fIpcSession->router, ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));

  if (zmq_bind(fIpcSession->router, fIpcSession->endpoint.c_str()) != 0) {
    const std::string err = zmq_strerror(zmq_errno());
    zmq_close(fIpcSession->router);
    zmq_ctx_term(fIpcSession->ctx);
    if (!isTcp) ::unlink(fIpcSession->endpointPath.c_str());
    fIpcSession.reset();
    throw std::runtime_error("Failed to bind endpoint '" + fIpcSession->endpoint + "': " + err);
  }

  fIpcSession->identityToWorker.clear();
  fIpcSession->identityToWorker.reserve(processesToUse);
  fIpcSession->workerIdentityVec.clear();
  fIpcSession->pendingReadyIdentities.clear();

  if (!isTcp) {
    // IPC/fork mode: pre-seed the map as before so WorkerLoop identities match
    for (size_t i = 0; i < processesToUse; ++i) {
      fIpcSession->identityToWorker[NDimensionalIpcRunner::BuildWorkerIdentity(i)] = i;
    }
  } else {
    // TCP mode: store context for late-joining workers
    fIpcSession->jobDir       = jobDir;
    fIpcSession->treeName     = treeName;
    fIpcSession->workerObjects = &workerObjects;
    fIpcSession->maxWorkers   = processesToUse;
    fIpcSession->macroList    = macroList;
    fIpcSession->tmpDir       = tmpDir;
    fIpcSession->tmpResultsDir = tmpResultsDir;
    fIpcSession->macroParams  = macroParams;
    fIpcSession->bootstrapAssignments.clear();
  }

  if (!isTcp) {
    fIpcSession->childPids.assign(processesToUse, -1);
    for (size_t i = 0; i < processesToUse; ++i) {
      pid_t pid = fork();
      if (pid < 0) {
        NDimensionalIpcRunner::CleanupChildProcesses(fIpcSession->childPids);
        zmq_close(fIpcSession->router);
        zmq_ctx_term(fIpcSession->ctx);
        ::unlink(fIpcSession->endpointPath.c_str());
        fIpcSession.reset();
        throw std::runtime_error("Failed to fork worker process.");
      }
      if (pid == 0) {
        zmq_close(fIpcSession->router);
        zmq_ctx_term(fIpcSession->ctx);
        const int rc = NDimensionalIpcRunner::WorkerLoop(fIpcSession->endpoint, i, workerObjects[i]);
        _exit(rc == 0 ? 0 : 1);
      }
      fIpcSession->childPids[i] = pid;
    }
  }

  // --- Wait for initial workers ---
  // IPC (fork): wait for all; TCP: wait until at least one connects or timeout.
  int readyTimeoutSec = isTcp ? 300 : 30;
  if (isTcp) {
    if (const char * env = gSystem->Getenv("NDMSPC_WORKER_TIMEOUT")) {
      try { readyTimeoutSec = std::stoi(env); } catch (...) {}
    }
    NLogInfo("NDimensionalExecutor::StartProcessIpc: waiting up to %d s for TCP workers (max %zu) ...",
             readyTimeoutSec, processesToUse);
  }
  const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(readyTimeoutSec);

  // IPC/fork mode needs the full fixed worker set before dispatch starts.
  // TCP mode is dynamic: start as soon as the first worker is ready, and let
  // additional workers join through the late-READY path during execution.
  const size_t readyTarget = isTcp ? 1 : processesToUse;

  while (fIpcSession->workerIdentityVec.size() < readyTarget) {
    if (isTcp && !fIpcSession->pendingReadyIdentities.empty()) {
      const std::string identity = fIpcSession->pendingReadyIdentities.front();
      fIpcSession->pendingReadyIdentities.erase(fIpcSession->pendingReadyIdentities.begin());
      InitTcpWorker(identity);
      continue;
    }

    std::vector<std::string> frames;
    if (!NDimensionalIpcRunner::ReceiveFrames(fIpcSession->router, frames)) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (std::chrono::steady_clock::now() > readyDeadline) {
          if (!isTcp) {
            NDimensionalIpcRunner::CleanupChildProcesses(fIpcSession->childPids);
            zmq_close(fIpcSession->router);
            zmq_ctx_term(fIpcSession->ctx);
            ::unlink(fIpcSession->endpointPath.c_str());
            fIpcSession.reset();
            throw std::runtime_error("Timeout while waiting for IPC workers to become ready.");
          }
          zmq_close(fIpcSession->router);
          zmq_ctx_term(fIpcSession->ctx);
          fIpcSession.reset();
          throw std::runtime_error("Timeout: no TCP workers connected.");
        }
        continue;
      }
      if (!isTcp) NDimensionalIpcRunner::CleanupChildProcesses(fIpcSession->childPids);
      zmq_close(fIpcSession->router);
      zmq_ctx_term(fIpcSession->ctx);
      if (!isTcp) ::unlink(fIpcSession->endpointPath.c_str());
      fIpcSession.reset();
      throw std::runtime_error("Failed to receive READY message from worker.");
    }
    if (frames.size() < 2) continue;
    const std::string & identity = frames[0];
    const std::string & cmd      = frames[1];
    if (isTcp && cmd == "BOOTSTRAP") {
      HandleBootstrap(identity);
      continue;
    }
    if (cmd != "READY") continue;

    if (isTcp) {
      InitTcpWorker(identity);
    } else {
      // IPC/fork: just register (already pre-seeded)
      if (fIpcSession->identityToWorker.count(identity)) {
        if (std::find(fIpcSession->workerIdentityVec.begin(), fIpcSession->workerIdentityVec.end(), identity)
            == fIpcSession->workerIdentityVec.end()) {
          fIpcSession->workerIdentityVec.push_back(identity);
          NLogInfo("NDimensionalExecutor::StartProcessIpc: worker '%s' ready (%zu / %zu)", identity.c_str(),
                   fIpcSession->workerIdentityVec.size(), processesToUse);
        }
      }
    }
  }

  // In TCP mode the startup loop exits as soon as the first worker is ready.
  // Any further READY messages that arrived (and were buffered in
  // pendingReadyIdentities during InitTcpWorker's ACK wait) must be drained
  // here so all already-connected workers are fully initialised before the
  // dispatch loop starts — otherwise their tasks would never be scheduled.
  if (isTcp) {
    while (!fIpcSession->pendingReadyIdentities.empty()) {
      const std::string id = fIpcSession->pendingReadyIdentities.front();
      fIpcSession->pendingReadyIdentities.erase(fIpcSession->pendingReadyIdentities.begin());
      InitTcpWorker(id);
    }
  }

  if (!isTcp) {
    InstallIpcSigIntHandler(fIpcSession->childPids, fIpcSession->oldSigIntAction, fIpcSession->hasOldSigIntAction);
  } else {
    // TCP mode: install handler with no child PIDs — just sets gIpcSigIntRequested
    // so the dispatch loop can break cleanly and FinishProcessIpc sends STOP to workers.
    InstallIpcSigIntHandler({}, fIpcSession->oldSigIntAction, fIpcSession->hasOldSigIntAction);
  }
}

// Centralized worker failure cleanup: recovers tasks, removes worker from tracking, logs, updates progress.
// This eliminates duplication across TCP send failure, TCP timeout, and IPC crash handlers.
size_t NDimensionalExecutor::HandleWorkerFailure(const std::string & failedIdentity,
                                                 const std::string & failureReason,
                                                 size_t & outstanding,
                                                 size_t & acked)
{
  if (!fIpcSession) {
    return 0;
  }

  size_t redistributedCount = 0;
  size_t replayedDoneCount  = 0;
  size_t replayedLiveCount  = 0;

  auto historyIt = fIpcSession->workerTaskHistory.find(failedIdentity);
  if (historyIt != fIpcSession->workerTaskHistory.end()) {
    for (const size_t taskId : historyIt->second) {
      const bool wasDone = fIpcSession->taskStateManager.IsDone(taskId);
      if (!fIpcSession->taskStateManager.RequeueTask(taskId)) {
        continue;
      }
      ++redistributedCount;
      if (wasDone) {
        ++replayedDoneCount;
      } else {
        ++replayedLiveCount;
      }
    }
    fIpcSession->workerTaskHistory.erase(historyIt);
  } else {
    const auto recovered = fIpcSession->taskStateManager.RecoverWorkerTasks(failedIdentity);
    redistributedCount   = recovered.size();
    replayedLiveCount    = redistributedCount;
  }

  if (replayedLiveCount > 0) {
    const size_t dec = std::min(outstanding, replayedLiveCount);
    outstanding -= dec;
  }
  if (replayedDoneCount > 0) {
    const size_t dec = std::min(acked, replayedDoneCount);
    acked -= dec;
  }

  // Remove worker from all tracking structures
  auto identityIt = std::find(fIpcSession->workerIdentityVec.begin(),
                             fIpcSession->workerIdentityVec.end(),
                             failedIdentity);
  if (identityIt != fIpcSession->workerIdentityVec.end()) {
    fIpcSession->workerIdentityVec.erase(identityIt);
  }
  fIpcSession->identityToWorker.erase(failedIdentity);
  fIpcSession->workerLastActivity.erase(failedIdentity);
  fIpcSession->failedTcpWorkers.erase(failedIdentity);

  // Log worker removal
  if (redistributedCount > 0) {
    if (failureReason == "send_failure") {
      NLogWarning("TCP worker '%s' removed due to send failure. Redistributing %zu task(s). Remaining workers: %zu",
                 failedIdentity.c_str(), redistributedCount, fIpcSession->workerIdentityVec.size());
    } else if (failureReason == "timeout") {
      NLogWarning("TCP worker '%s' inactive/disconnected (%zu tasks pending). Redistributing to remaining %zu workers.",
                 failedIdentity.c_str(), redistributedCount, fIpcSession->workerIdentityVec.size());
    } else if (failureReason == "crash") {
      NLogWarning("Worker process '%s' exited unexpectedly. Redistributing %zu task(s) to remaining %zu workers.",
                 failedIdentity.c_str(), redistributedCount, fIpcSession->workerIdentityVec.size());
    } else if (failureReason == "interrupted") {
      NLogWarning("Worker '%s' interrupted. Replaying %zu task(s) on remaining %zu worker(s).",
                 failedIdentity.c_str(), redistributedCount, fIpcSession->workerIdentityVec.size());
    } else {
      NLogWarning("Worker '%s' failed (%s). Redistributing %zu task(s). Remaining workers: %zu",
                 failedIdentity.c_str(), failureReason.c_str(), redistributedCount, fIpcSession->workerIdentityVec.size());
    }
  }

  return redistributedCount;
}

size_t NDimensionalExecutor::ExecuteCurrentBoundsProcessIpc(const std::string & definitionName,
                                                            const std::vector<Long64_t> * definitionIds,
                                                            const std::function<void(const ExecutionProgress&)> & progressCallback)
{
  if (!fIpcSession) {
    throw std::runtime_error("IPC session is not active.");
  }
  if (fNumDimensions == 0) {
    return 0;
  }

  fIpcSession->taskStateManager.Clear();
  fIpcSession->workerTaskHistory.clear();

  // Save current definition so late-joining workers can catch up
  fIpcSession->currentDefName    = definitionName;
  fIpcSession->currentDefIds     = definitionIds ? *definitionIds : std::vector<Long64_t>{};
  fIpcSession->hasCurrentDefIds  = (definitionIds != nullptr);

  if (!definitionName.empty()) {
    std::vector<std::string> failedWorkers;
    for (const auto & identity : fIpcSession->workerIdentityVec) {
      if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "SETDEF", definitionName})) {
        if (fIpcSession->isTcp) {
          NLogWarning("Failed to send SETDEF to TCP worker '%s', marking as failed", identity.c_str());
          failedWorkers.push_back(identity);
          continue; // Skip SETIDS for this worker and continue with others
        } else {
          throw std::runtime_error("Failed to send IPC SETDEF message to worker '" + identity + "'.");
        }
      }
      if (definitionIds != nullptr) {
        if (!NDimensionalIpcRunner::SendFrames(
                fIpcSession->router, {identity, "SETIDS", NDimensionalIpcRunner::SerializeIds(*definitionIds)})) {
          if (fIpcSession->isTcp) {
            NLogWarning("Failed to send SETIDS to TCP worker '%s', marking as failed", identity.c_str());
            failedWorkers.push_back(identity);
            continue;
          } else {
            throw std::runtime_error("Failed to send IPC SETIDS message to worker '" + identity + "'.");
          }
        }
      }
    }
    
    // Mark failed TCP workers for removal
    if (fIpcSession->isTcp && !failedWorkers.empty()) {
      for (const auto & identity : failedWorkers) {
        fIpcSession->failedTcpWorkers.insert(identity);
      }
      NLogWarning("Marked %zu TCP worker(s) as failed during SETDEF/SETIDS", failedWorkers.size());
    }
  }

  size_t ipcBatchSize = 1;
  if (const char * envBatchSize = gSystem->Getenv("NDMSPC_IPC_BATCH_SIZE")) {
    try {
      ipcBatchSize = std::max<size_t>(1, static_cast<size_t>(std::stoll(envBatchSize)));
    }
    catch (...) {
      NLogWarning("NGnTree::Process: Invalid NDMSPC_IPC_BATCH_SIZE='%s', using default=%zu", envBatchSize,
                  ipcBatchSize);
    }
  }

  size_t            nextTaskId        = 0;
  size_t            dispatchMessageId = 0;
  size_t            outstanding       = 0;
  size_t            outstandingMessages = 0;
  size_t            acked             = 0;
  size_t            nextSchedulerLogAck = 200;
  std::string       firstError;
  // Late-joining TCP workers: identity → assigned worker index.
  // INIT is sent immediately on READY but the ACK is handled asynchronously
  // in the main receive loop so we never block on socket reads mid-dispatch.
  std::unordered_map<std::string, size_t> pendingInitWorkers;

  int stallTimeoutSec = 120;
  if (const char * envStallTimeout = gSystem->Getenv("NDMSPC_IPC_STALL_TIMEOUT")) {
    try {
      stallTimeoutSec = std::max(5, std::stoi(envStallTimeout));
    }
    catch (...) {
      NLogWarning("NGnTree::Process: Invalid NDMSPC_IPC_STALL_TIMEOUT='%s', using default=%d", envStallTimeout,
                  stallTimeoutSec);
    }
  }
  auto lastProgress = std::chrono::steady_clock::now();

  fCurrentCoords = fMinBounds;
  bool hasMore = true;

  // maxInFlightMessages is recalculated dynamically as workers join
  size_t       totalTasks = 1;
  for (size_t i = 0; i < fNumDimensions; ++i) {
    totalTasks *= static_cast<size_t>(fMaxBounds[i] - fMinBounds[i] + 1);
  }
  auto isUserInterrupted = []() {
    if (gIpcSigIntRequested != 0) return true;
    return (gROOT && gROOT->IsInterrupted());
  };
  // Helper: send SETDEF/SETIDS catchup to a newly-joined worker
  auto sendCatchup = [&](const std::string & identity) {
    if (!fIpcSession->currentDefName.empty()) {
      NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "SETDEF", fIpcSession->currentDefName});
      if (fIpcSession->hasCurrentDefIds) {
        NDimensionalIpcRunner::SendFrames(
          fIpcSession->router, {identity, "SETIDS", NDimensionalIpcRunner::SerializeIds(fIpcSession->currentDefIds)});
      }
    }
  };

  while ((hasMore || outstanding > 0 || fIpcSession->taskStateManager.HasPending()) && firstError.empty()) {
    // STATE MACHINE FOR TASK DISTRIBUTION:
    // pending → dispatch → running → ACK → done
    //
    // Key counters:
    // - hasMore: More tasks available from coordinate iterator?
    // - outstanding: Number of tasks sent but not yet ACKed
    // - pending: Tasks queued, not yet assigned to workers
    // - running: Tasks assigned to workers, awaiting ACK
    // - done: Tasks fully processed and ACKed
    // - acked: Cumulative count of tasks ACKed (used for progress callback)
    //
    // SAFE EXIT CONDITIONS:
    // 1. hasMore=false && outstanding=0 && pending=0 → All work complete (SUCCESS)
    // 2. No workers remain && (outstanding>0 || pending>0) → FAIL (work lost)
    // 3. User interrupt signal (Ctrl+C) → FAIL with cleanup
    // 4. Stall timeout (no progress for N seconds) → FAIL (deadlock)
    //
    // WORKER FAILURE RECOVERY:
    // On TCP timeout, IPC crash, or send failure:
    // - Call RecoverWorkerTasks(worker) → Returns pending tasks, requeues running tasks
    // - Tasks become pending again and are re-dispatched to remaining workers
    // - No loss of work, but may increase total execution time
    // - No retry loop to avoid infinite loops with persistent failures
    
    if (isUserInterrupted()) {
      firstError = "Interrupted by user";
      break;
    }

    // Check if all workers have failed - exit early to avoid infinite loop
    if (fIpcSession->workerIdentityVec.empty() && (outstanding > 0 || fIpcSession->taskStateManager.HasPending() || hasMore)) {
      if (fIpcSession->isTcp) {
        firstError = "No workers available. All TCP workers have disconnected/failed.";
      } else {
        firstError = "No workers available. All worker processes have exited/failed.";
      }
      break;
    }

    // Allow multiple batches to be in flight for better parallelism
    // Each worker can have up to this many batches pending
    const size_t maxInFlightMessages = std::max<size_t>(4, fIpcSession->workerIdentityVec.size());

    while ((hasMore || fIpcSession->taskStateManager.HasPending()) && outstandingMessages < maxInFlightMessages && firstError.empty()) {
      if (fIpcSession->workerIdentityVec.empty()) break; // no workers yet — wait
      
      // Skip workers that have been marked as failed
      size_t workerSlot = dispatchMessageId % fIpcSession->workerIdentityVec.size();
      std::string identity = fIpcSession->workerIdentityVec[workerSlot];
      
      // In TCP mode, skip failed workers and find next available worker
      if (fIpcSession->isTcp) {
        size_t attempts = 0;
        while (fIpcSession->failedTcpWorkers.count(identity) && attempts < fIpcSession->workerIdentityVec.size()) {
          ++dispatchMessageId;
          ++attempts;
          workerSlot = dispatchMessageId % fIpcSession->workerIdentityVec.size();
          identity = fIpcSession->workerIdentityVec[workerSlot];
        }
        // If all workers are failed, break to trigger cleanup
        if (attempts >= fIpcSession->workerIdentityVec.size()) {
          break;
        }
      }
      
      std::vector<std::pair<size_t, std::vector<int>>> batchTasks;
      const size_t nw              = fIpcSession->workerIdentityVec.size();
      const size_t remainingTasks  = (nextTaskId < totalTasks) ? (totalTasks - nextTaskId) : 0;
      const size_t adaptiveBatchSize = std::max<size_t>(
          1, std::min(ipcBatchSize, std::max<size_t>(1, (remainingTasks + nw - 1) / nw)));
      batchTasks.reserve(adaptiveBatchSize);

      // First, dispatch redistributed (pending) tasks from failed workers
      // These were added back to pending state by RecoverWorkerTasks or MarkFailed
      size_t reprocessedCount = 0;
      size_t redistPerBatch = 1;
      if (adaptiveBatchSize > 1 && fIpcSession->workerIdentityVec.size() > 0) {
        redistPerBatch = std::max<size_t>(1, adaptiveBatchSize / fIpcSession->workerIdentityVec.size());
      }
      size_t redistAdded = 0;
      while (fIpcSession->taskStateManager.HasPending() && outstanding < maxInFlightMessages * ipcBatchSize && 
             batchTasks.size() < adaptiveBatchSize && redistAdded < redistPerBatch) {
        size_t            taskId = 0;
        std::vector<int>  coords;
        if (!fIpcSession->taskStateManager.ClaimNextPendingForWorker(identity, taskId, coords)) {
          break;
        }
        batchTasks.emplace_back(taskId, coords);
        fIpcSession->workerTaskHistory[identity].insert(taskId);
        ++redistAdded;
        ++outstanding;

        if (isUserInterrupted()) {
          firstError = "Interrupted by user";
          break;
        }
      }
      
      if (reprocessedCount > 0) {
        NLogDebug("Redistributing %zu previously-completed tasks (acked counter decremented)", reprocessedCount);
      }

      // Then, dispatch new tasks if space available
      while (hasMore && outstanding < maxInFlightMessages * ipcBatchSize && batchTasks.size() < adaptiveBatchSize) {
        fIpcSession->taskStateManager.AddPending(nextTaskId, fCurrentCoords);
        size_t            taskId = 0;
        std::vector<int>  payload;
        if (!fIpcSession->taskStateManager.ClaimNextPendingForWorker(identity, taskId, payload)) {
          firstError = "Failed to claim pending task for worker dispatch.";
          break;
        }
        batchTasks.emplace_back(taskId, payload);
        fIpcSession->workerTaskHistory[identity].insert(taskId);
        ++nextTaskId;
        ++outstanding;

        if (!Increment()) {
          hasMore = false;
        }

        if (isUserInterrupted()) {
          firstError = "Interrupted by user";
          break;
        }
      }

      if (!firstError.empty()) {
        break;
      }

      if (batchTasks.empty()) {
        continue;
      }

      // Log assigned task coordinates to supervisor console for debugging
      for (const auto & task : batchTasks) {
        const std::string coordsStr = NDimensionalIpcRunner::SerializeCoords(task.second);
        NLogInfo("NDimensionalExecutor: Assign task %zu coords=%s -> worker '%s'", task.first, coordsStr.c_str(), identity.c_str());
      }

      // Initialize/update activity tracking for TCP workers
      if (fIpcSession->isTcp) {
        fIpcSession->workerLastActivity[identity] = std::chrono::steady_clock::now();
      }

      if (batchTasks.size() == 1) {
        const std::string taskId = std::to_string(batchTasks[0].first);
        const std::string coords = NDimensionalIpcRunner::SerializeCoords(batchTasks[0].second);
        if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "TASK", taskId, coords})) {
          if (fIpcSession->isTcp) {
            // TCP worker likely disconnected - mark for redistribution
            NLogWarning("Failed to send TASK to TCP worker '%s', marking as failed", identity.c_str());

            fIpcSession->failedTcpWorkers.insert(identity);
            // Put task back in redistribution queue (mark as failed to return to pending)
            for (const auto & task : batchTasks) {
              fIpcSession->taskStateManager.MarkFailed(task.first);
            }
            break; // Break inner loop to skip this worker and retry on next iteration
          } else {
            firstError = "Failed to send IPC TASK message to worker '" + identity + "'.";
            break;
          }
        }
      }
      else {
        std::ostringstream payload;
        for (size_t i = 0; i < batchTasks.size(); ++i) {
          if (i != 0) payload << ';';
          payload << batchTasks[i].first << ':' << NDimensionalIpcRunner::SerializeCoords(batchTasks[i].second);
        }
        if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "TASKB", payload.str()})) {
          if (fIpcSession->isTcp) {
            // TCP worker likely disconnected - mark for redistribution
            NLogWarning("Failed to send TASKB to TCP worker '%s', marking as failed", identity.c_str());

            fIpcSession->failedTcpWorkers.insert(identity);
            // Put tasks back in redistribution queue (mark as failed to return to pending)
            for (const auto & task : batchTasks) {
              fIpcSession->taskStateManager.MarkFailed(task.first);
            }
            break; // Break inner loop to skip this worker and retry on next iteration
          } else {
            firstError = "Failed to send IPC TASKB message to worker '" + identity + "'.";
            break;
          }
        }
      }
      ++dispatchMessageId;
      ++outstandingMessages;
    }

    // Clean up any TCP workers that failed during send attempts
    if (fIpcSession->isTcp && !fIpcSession->failedTcpWorkers.empty()) {
      std::vector<std::string> workersToRemove;
      for (const auto & identity : fIpcSession->workerIdentityVec) {
        if (fIpcSession->failedTcpWorkers.count(identity)) {
          workersToRemove.push_back(identity);
        }
      }
      
      for (const auto & failedIdentity : workersToRemove) {
        HandleWorkerFailure(failedIdentity, "send_failure", outstanding, acked);
        
        // Update progress bar to reflect worker count change
        if (progressCallback) {
          ExecutionProgress progress{acked, fIpcSession->taskStateManager.PendingCount(),
                                    fIpcSession->taskStateManager.RunningCount(),
                                    fIpcSession->taskStateManager.DoneCount(),
                                    fIpcSession->workerIdentityVec.size()};
          progressCallback(progress);
        }
      }
      
      // If no workers remain, fail
      if (fIpcSession->workerIdentityVec.empty()) {
        firstError = "No workers available. All TCP workers have disconnected/failed.";

      }
    }

    if (outstanding == 0 && fIpcSession->workerIdentityVec.empty()) continue;
    if (outstanding == 0 && !hasMore && !fIpcSession->taskStateManager.HasPending()) continue;

    std::vector<std::string> frames;
    if (!NDimensionalIpcRunner::ReceiveFrames(fIpcSession->router, frames)) {
      if (errno == EINTR || isUserInterrupted()) {
        firstError = "Interrupted by user";
        break;
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // In TCP mode, receive errors can occur when workers disconnect
        // Let the activity timeout detection handle this gracefully
        if (!fIpcSession->isTcp) {
          firstError = "Failed to receive IPC ACK/ERR from worker.";
          break;
        }
        // TCP mode: treat as timeout and continue to worker failure detection
      }

      // Check for worker failures and redistribute their tasks
      // For TCP mode: check for inactive workers (no ACKs received)
      if (fIpcSession->isTcp && outstanding > 0) {
        const auto now = std::chrono::steady_clock::now();
        int tcpWorkerTimeoutSec = 30; // Default timeout for TCP worker inactivity
        if (const char * envTcpTimeout = gSystem->Getenv("NDMSPC_TCP_WORKER_TIMEOUT")) {
          try {
            tcpWorkerTimeoutSec = std::max(10, std::stoi(envTcpTimeout));
          } catch (...) {}
        }

        std::vector<std::string> inactiveWorkers;
        for (const auto & identity : fIpcSession->workerIdentityVec) {
          auto activityIt = fIpcSession->workerLastActivity.find(identity);
          if (activityIt != fIpcSession->workerLastActivity.end()) {
            auto inactiveSecs = std::chrono::duration_cast<std::chrono::seconds>(now - activityIt->second).count();
            if (inactiveSecs >= tcpWorkerTimeoutSec) {
              // Check if this worker has pending tasks - if so, it should have responded by now
              bool hasPendingTasks = !fIpcSession->taskStateManager.GetWorkerTasks(identity).empty();
              
              // Mark as inactive if:
              // 1. It has pending tasks (should have responded by now), OR
              // 2. It's been idle for more than 2x the timeout (likely disconnected)
              if (hasPendingTasks || inactiveSecs >= tcpWorkerTimeoutSec * 2) {
                inactiveWorkers.push_back(identity);
              }
            }
          }
        }

        // Redistribute tasks from inactive TCP workers
        for (const auto & failedIdentity : inactiveWorkers) {
          if (fIpcSession->failedTcpWorkers.count(failedIdentity)) continue; // already handled
          fIpcSession->failedTcpWorkers.insert(failedIdentity);

          HandleWorkerFailure(failedIdentity, "timeout", outstanding, acked);

          // Reset progress timer since we're actively handling worker failure
          lastProgress = std::chrono::steady_clock::now();
          
          // Update progress bar to reflect worker count change
          if (progressCallback) {
            ExecutionProgress progress{acked, fIpcSession->taskStateManager.PendingCount(),
                                      fIpcSession->taskStateManager.RunningCount(),
                                      fIpcSession->taskStateManager.DoneCount(),
                                      fIpcSession->workerIdentityVec.size()};
            progressCallback(progress);
          }

          // If no workers remain, fail
          if (fIpcSession->workerIdentityVec.empty()) {
            firstError = "All TCP workers have disconnected/failed. No workers available to continue processing.";

            break;
          }
        }
      }

      // For IPC mode: check forked child processes
      for (size_t i = 0; i < fIpcSession->childPids.size(); ++i) {
        int   status = 0;
        pid_t rc     = waitpid(fIpcSession->childPids[i], &status, WNOHANG);
        if (rc == fIpcSession->childPids[i]) {
          // Worker process exited - redistribute its pending tasks
          std::string failedIdentity = NDimensionalIpcRunner::BuildWorkerIdentity(i);

          HandleWorkerFailure(failedIdentity, "crash", outstanding, acked);
          
          // Reset progress timer since we're actively redistributing
          lastProgress = std::chrono::steady_clock::now();
          
          // Update progress bar to reflect worker count change
          if (progressCallback) {
            ExecutionProgress progress{acked, fIpcSession->taskStateManager.PendingCount(),
                                      fIpcSession->taskStateManager.RunningCount(),
                                      fIpcSession->taskStateManager.DoneCount(),
                                      fIpcSession->workerIdentityVec.size()};
            progressCallback(progress);
          }
          
          // If no workers remain, fail
          if (fIpcSession->workerIdentityVec.empty()) {
            firstError = "No workers available. All worker processes have exited/failed.";

            break;
          }
        }
      }
      if (firstError.empty() && outstanding > 0 && stallTimeoutSec > 0) {
        const auto now       = std::chrono::steady_clock::now();
        const auto stallSecs = std::chrono::duration_cast<std::chrono::seconds>(now - lastProgress).count();
        if (stallSecs >= stallTimeoutSec) {
          const size_t activeWorkers = fIpcSession->workerIdentityVec.size();
          if (activeWorkers == 0) {
            firstError = "No workers available. All workers have disconnected/failed with " +
                         std::to_string(outstanding) + " pending tasks remaining.";
          } else {
            firstError = "No IPC/TCP ACK progress for " + std::to_string(stallSecs) + "s with " +
                         std::to_string(outstanding) + " pending tasks (active workers: " + 
                         std::to_string(activeWorkers) + ").";
          }
        }
      }
      continue;
    }

    if (frames.size() < 2) continue;

    // Handle late-joining TCP worker: send INIT immediately but do NOT block
    // waiting for the ACK — the ACK is handled below as a 2-frame message so
    // task ACKs from active workers are never dropped.
    if (fIpcSession->isTcp && frames.size() >= 2 && frames[1] == "READY") {
      const std::string & lateId = frames[0];
      if (!fIpcSession->identityToWorker.count(lateId) && !pendingInitWorkers.count(lateId)) {
        const std::string prefix    = "wk_";
        const size_t      prefixLen = prefix.size();
        if (lateId.size() > prefixLen && lateId.substr(0, prefixLen) == prefix) {
          size_t workerIdx = std::numeric_limits<size_t>::max();
          try { workerIdx = std::stoul(lateId.substr(prefixLen)); } catch (...) {}
          if (workerIdx < fIpcSession->maxWorkers) {
            const std::string sessionId = std::to_string(getpid());
            if (NDimensionalIpcRunner::SendFrames(fIpcSession->router,
                                                  {lateId, "INIT", std::to_string(workerIdx), sessionId,
                                                   fIpcSession->jobDir, fIpcSession->treeName,
                                                   fIpcSession->tmpDir, fIpcSession->tmpResultsDir})) {
              pendingInitWorkers[lateId] = workerIdx;
              NLogDebug("NDimensionalExecutor: late worker '%s' sent INIT, awaiting ACK", lateId.c_str());
            }
          }
        }
      }
      lastProgress = std::chrono::steady_clock::now();
      continue;
    }

    // Handle INIT ACK from a late-joining worker (2-frame: identity + "ACK").
    if (fIpcSession->isTcp && frames.size() == 2 && frames[1] == "ACK") {
      auto pit = pendingInitWorkers.find(frames[0]);
      if (pit != pendingInitWorkers.end()) {
        const std::string & id  = pit->first;
        const size_t        idx = pit->second;
        
        // Check for duplicate identity (defensive - shouldn't happen with proper cleanup)
        if (fIpcSession->identityToWorker.count(id)) {
          NLogWarning("NDimensionalExecutor: late worker '%s' already registered, replacing", id.c_str());
          // Remove from vector if present
          auto it = std::find(fIpcSession->workerIdentityVec.begin(), fIpcSession->workerIdentityVec.end(), id);
          if (it != fIpcSession->workerIdentityVec.end()) {
            fIpcSession->workerIdentityVec.erase(it);
          }
        }
        
        fIpcSession->identityToWorker[id] = idx;
        fIpcSession->workerIdentityVec.push_back(id);
        // Initialize activity tracking for this new TCP worker
        fIpcSession->workerLastActivity[id] = std::chrono::steady_clock::now();
        NLogInfo("NDimensionalExecutor: late worker '%s' (idx=%zu) joined [total: %zu]",
                 id.c_str(), idx, fIpcSession->workerIdentityVec.size());
        // Log in-flight task distribution across all workers so the startup imbalance is visible
        for (const auto & wid : fIpcSession->workerIdentityVec) {
          const size_t inFlight = fIpcSession->taskStateManager.GetWorkerTasks(wid).size();
          NLogInfo("NDimensionalExecutor: in-flight distribution: worker '%s' has %zu pending task(s)", wid.c_str(), inFlight);
        }
        sendCatchup(id);
        pendingInitWorkers.erase(pit);
        lastProgress = std::chrono::steady_clock::now();
        continue;
      }
      // Not a pending-init ACK — fall through to the malformed-message guard.
    }

    // Handle bootstrapping worker requesting config
    if (fIpcSession->isTcp && frames.size() >= 2 && frames[1] == "BOOTSTRAP") {
      HandleBootstrap(frames[0]);
      lastProgress = std::chrono::steady_clock::now();
      continue;
    }

    // Handle worker shutdown notification (e.g., Ctrl+C)
    if (frames.size() >= 2 && frames[1] == "SHUTDOWN") {
      const std::string & workerIdentity = frames[0];
      const std::string reason = (frames.size() >= 3) ? frames[2] : "unknown";
      const std::string tasksCompleted = (frames.size() >= 4) ? frames[3] : "?";
      
      NLogWarning("Worker '%s' reported shutdown: %s (completed %s tasks)", workerIdentity.c_str(), reason.c_str(), tasksCompleted.c_str());

      if (fIpcSession->isTcp) {
        fIpcSession->failedTcpWorkers.insert(workerIdentity);
      }

      HandleWorkerFailure(workerIdentity, "interrupted", outstanding, acked);

      if (progressCallback) {
        ExecutionProgress progress{acked, fIpcSession->taskStateManager.PendingCount(),
                                  fIpcSession->taskStateManager.RunningCount(),
                                  fIpcSession->taskStateManager.DoneCount(),
                                  fIpcSession->workerIdentityVec.size()};
        progressCallback(progress);
      }
      
      // If no workers remain, fail
      if (fIpcSession->workerIdentityVec.empty()) {
        if (fIpcSession->isTcp) {
          firstError = "No workers available. All TCP workers have shut down.";
        } else {
          firstError = "No workers available. All worker processes have shut down.";
        }

        break;
      }
      
      lastProgress = std::chrono::steady_clock::now();
      continue;
    }

    if (frames.size() == 2 && frames[1] == "DONE") {
      const std::string & workerIdentity = frames[0];
      if (fIpcSession->identityToWorker.count(workerIdentity)) {
        fIpcSession->earlyDoneWorkers.insert(workerIdentity);
        NLogDebug("NDimensionalExecutor::IPC: Worker '%s' sent DONE before FinishProcessIpc; deferring",
                  workerIdentity.c_str());
      } else {
        NLogWarning("NDimensionalExecutor::IPC: ignoring DONE from unknown worker '%s'", workerIdentity.c_str());
      }
      lastProgress = std::chrono::steady_clock::now();
      continue;
    }

    if (frames.size() < 3) {
      // A 2-frame ACK that isn't a pending-init reply is unexpected.
      if (frames.size() == 2 && frames[1] == "ACK")
        NLogWarning("NDimensionalExecutor: unexpected 2-frame ACK from '%s', ignoring", frames[0].c_str());
      else
        firstError = "Malformed IPC message received from worker.";
      continue;
    }

    const std::string & cmd = frames[1];
    if (cmd == "ACK") {
      const std::string & workerIdentity = frames[0];
      size_t taskId = 0;
      try {
        taskId = static_cast<size_t>(std::stoull(frames[2]));
      }
      catch (...) {
        firstError = "Malformed IPC task id received from worker.";
        break;
      }

      // Mark task as done using TaskStateManager
      if (!fIpcSession->taskStateManager.MarkDone(taskId)) {
        firstError = "Received ACK for unknown or already-done task " + std::to_string(taskId) + ".";
        break;
      }

      // Update TCP worker activity tracking
      if (fIpcSession->isTcp) {
        fIpcSession->workerLastActivity[workerIdentity] = std::chrono::steady_clock::now();
      }
      
      if (outstanding == 0) {
        firstError = "IPC outstanding counter underflow while processing ACK.";
        break;
      }
      if (outstandingMessages == 0) {
        firstError = "IPC message outstanding counter underflow while processing ACK.";
        break;
      }
      --outstanding;
      --outstandingMessages;
      ++acked;
      lastProgress = std::chrono::steady_clock::now();
      const size_t activeWorkersNow = fIpcSession->workerIdentityVec.size();
      if (progressCallback) {
        ExecutionProgress progress{acked, fIpcSession->taskStateManager.PendingCount(),
                                  fIpcSession->taskStateManager.RunningCount(),
                                  fIpcSession->taskStateManager.DoneCount(),
                                  activeWorkersNow};
        progressCallback(progress);
      }
      if (acked >= nextSchedulerLogAck) {
        NLogDebug("NDimensionalExecutor::IPC: acked=%zu/%zu activeWorkers=%zu inFlightMessages=%zu pending=%zu running=%zu done=%zu",
                  acked, totalTasks, activeWorkersNow, outstandingMessages,
                  fIpcSession->taskStateManager.PendingCount(),
                  fIpcSession->taskStateManager.RunningCount(),
                  fIpcSession->taskStateManager.DoneCount());
        nextSchedulerLogAck += 200;
      }
      continue;
    }

    if (cmd == "ACKB") {
      const std::string & workerIdentity = frames[0];
      if (frames.size() < 3 || frames[2].empty()) {
        firstError = "Malformed IPC ACKB payload received from worker.";
        break;
      }

      if (outstandingMessages == 0) {
        firstError = "IPC message outstanding counter underflow while processing ACKB.";
        break;
      }
      --outstandingMessages;

      std::stringstream ackStream(frames[2]);
      std::string       ackToken;
      while (std::getline(ackStream, ackToken, ',')) {
        if (ackToken.empty()) continue;
        size_t ackTaskId = 0;
        try {
          ackTaskId = static_cast<size_t>(std::stoull(ackToken));
        }
        catch (...) {
          firstError = "Malformed IPC ACKB task id received from worker.";
          break;
        }

        // Mark task as done using TaskStateManager
        if (!fIpcSession->taskStateManager.MarkDone(ackTaskId)) {
          firstError = "Received ACKB for unknown or already-done task " + std::to_string(ackTaskId) + ".";
          break;
        }

        // Update TCP worker activity tracking
        if (fIpcSession->isTcp) {
          fIpcSession->workerLastActivity[workerIdentity] = std::chrono::steady_clock::now();
        }
        
        if (outstanding == 0) {
          firstError = "IPC outstanding counter underflow while processing ACKB.";
          break;
        }
        --outstanding;
        ++acked;
        lastProgress = std::chrono::steady_clock::now();
        const size_t activeWorkersNow = fIpcSession->workerIdentityVec.size();
        if (progressCallback) {
          ExecutionProgress progress{acked, fIpcSession->taskStateManager.PendingCount(),
                                    fIpcSession->taskStateManager.RunningCount(),
                                    fIpcSession->taskStateManager.DoneCount(),
                                    activeWorkersNow};
          progressCallback(progress);
        }
        if (acked >= nextSchedulerLogAck) {
          NLogDebug(
              "NDimensionalExecutor::IPC: acked=%zu/%zu activeWorkers=%zu inFlightMessages=%zu pending=%zu running=%zu done=%zu",
              acked, totalTasks, activeWorkersNow, outstandingMessages, 
              fIpcSession->taskStateManager.PendingCount(),
              fIpcSession->taskStateManager.RunningCount(),
              fIpcSession->taskStateManager.DoneCount());
          nextSchedulerLogAck += 200;
        }
      }

      if (!firstError.empty()) {
        break;
      }
      continue;
    }

    if (cmd == "ERR") {
      size_t taskId = 0;
      try {
        taskId = static_cast<size_t>(std::stoull(frames[2]));
      }
      catch (...) {
        firstError = "Malformed IPC task id received from worker.";
        break;
      }
      std::string errMsg = (frames.size() >= 4) ? frames[3] : "worker error";
      firstError         = "Worker reported error for task " + std::to_string(taskId) + ": " + errMsg;
      break;
    }

    firstError = "Unknown IPC command from worker: " + cmd;
    break;
  }

  if (!firstError.empty()) {
    throw std::runtime_error(firstError);
  }

  // All tasks should be either done or somehow lost - check state
  const size_t pendingCount = fIpcSession->taskStateManager.PendingCount();
  const size_t runningCount = fIpcSession->taskStateManager.RunningCount();
  if (pendingCount > 0 || runningCount > 0) {
    throw std::runtime_error("IPC execution finished with " + std::to_string(pendingCount) + " pending and " +
                             std::to_string(runningCount) + " running tasks still unacknowledged.");
  }

  return acked;
}

void NDimensionalExecutor::FinishProcessIpc(bool abort)
{
  if (!fIpcSession) {
    return;
  }

  const std::string stopReason = abort ? "abort" : "ok";
  for (const auto & it : fIpcSession->identityToWorker) {
    NDimensionalIpcRunner::SendFrames(fIpcSession->router, {it.first, "STOP", stopReason});
  }

  if (!fIpcSession->isTcp) {
    const bool exitedCleanly = NDimensionalIpcRunner::WaitForChildProcesses(fIpcSession->childPids, 1500);
    if (!exitedCleanly) {
      NDimensionalIpcRunner::CleanupChildProcesses(fIpcSession->childPids);
    }
  } else if (!abort) {
    // TCP mode normal finish: wait for DONE from all workers before merging
    const size_t      nWorkers    = fIpcSession->identityToWorker.size();
    std::set<std::string> doneWorkers;
    for (const auto & workerIdentity : fIpcSession->earlyDoneWorkers) {
      if (fIpcSession->identityToWorker.count(workerIdentity)) {
        doneWorkers.insert(workerIdentity);
      }
    }
    const auto        doneDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (doneWorkers.size() < nWorkers) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(doneDeadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0) {
        NLogWarning("NDimensionalExecutor::FinishProcessIpc: Timeout waiting for DONE from TCP workers (%zu/%zu received)",
                    doneWorkers.size(), nWorkers);
        break;
      }
      zmq_pollitem_t item = {fIpcSession->router, 0, ZMQ_POLLIN, 0};
      const int      rc   = zmq_poll(&item, 1, static_cast<long>(remaining));
      if (rc <= 0) {
        NLogWarning("NDimensionalExecutor::FinishProcessIpc: Timeout waiting for DONE from TCP workers (%zu/%zu received)",
                    doneWorkers.size(), nWorkers);
        break;
      }
      std::vector<std::string> frames;
      if (!NDimensionalIpcRunner::ReceiveFrames(fIpcSession->router, frames) || frames.size() < 2) continue;
      if (frames[1] == "DONE") {
        const std::string & workerIdentity = frames[0];
        doneWorkers.insert(workerIdentity);
        
        NLogDebug("NDimensionalExecutor::FinishProcessIpc: Worker '%s' sent DONE (%zu/%zu)", workerIdentity.c_str(),
                  doneWorkers.size(), nWorkers);
      } else if (frames[1] == "READY") {
        // Worker arrived after all tasks were dispatched — send STOP immediately.
        NLogInfo("NDimensionalExecutor::FinishProcessIpc: Late worker '%s' arrived, sending STOP", frames[0].c_str());
        NDimensionalIpcRunner::SendFrames(fIpcSession->router, {frames[0], "STOP", "ok"});
      } else if (frames[1] == "BOOTSTRAP") {
        // Worker is still bootstrapping — reply with CONFIG and a STOP will follow via READY.
        HandleBootstrap(frames[0]);
      }
    }
  }
  // Bound socket close time to avoid hangs at process end when peers disappear
  // or when STOP/DONE frames are still queued.
  if (fIpcSession->router) {
    int lingerMs = 0;
    if (fIpcSession->isTcp && abort) {
      // In TCP abort, allow a short grace period to flush STOP frames.
      lingerMs = 2000;
    }
    zmq_setsockopt(fIpcSession->router, ZMQ_LINGER, &lingerMs, sizeof(lingerMs));
  }

  if (fIpcSession->router) {
    zmq_close(fIpcSession->router);
  }
  if (fIpcSession->ctx) {
    zmq_ctx_term(fIpcSession->ctx);
  }
  if (!fIpcSession->isTcp && !fIpcSession->endpointPath.empty()) {
    ::unlink(fIpcSession->endpointPath.c_str());
  }
  // Capture registered worker indices before releasing the session.
  fRegisteredWorkerIndices.clear();
  if (fIpcSession) {
    for (const auto & kv : fIpcSession->identityToWorker) {
      fRegisteredWorkerIndices.insert(kv.second);
    }
  }
  RestoreIpcSigIntHandler(fIpcSession->oldSigIntAction, fIpcSession->hasOldSigIntAction);
  fIpcSession.reset();
}

template void NDimensionalExecutor::ExecuteParallel<NGnThreadData>(
    const std::function<void(const std::vector<int> & coords, NGnThreadData & thread_object)> & func,
    std::vector<NGnThreadData> &                                                                  thread_objects);

} // namespace Ndmspc
