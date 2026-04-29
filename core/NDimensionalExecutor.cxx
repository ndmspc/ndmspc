#include <string>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <csignal>
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
  std::string tmpDir;          // supervisor's NDMSPC_TMP_DIR (fallback for workers)
  std::string tmpResultsDir;   // supervisor's NDMSPC_TMP_RESULTS_DIR
  size_t      bootstrapNextIdx{0}; // auto-assigned index counter for BOOTSTRAP
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
    oss << "wk_" << std::setw(3) << std::setfill('0') << md->GetAssignedIndex();

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

  const auto initDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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
    if (ackFrames.size() >= 2 && ackFrames[0] == identity && ackFrames[1] == "ACK") {
      acked = true;
    }
  }
  if (!acked) {
    NLogError("NDimensionalExecutor::InitTcpWorker: worker '%s' did not ACK INIT", identity.c_str());
    return false;
  }

  fIpcSession->identityToWorker[identity] = workerIdx;
  fIpcSession->workerIdentityVec.push_back(identity);
  NLogInfo("NDimensionalExecutor::InitTcpWorker: worker '%s' (idx=%zu) joined [total: %zu]",
           identity.c_str(), workerIdx, fIpcSession->workerIdentityVec.size());
  return true;
}

bool NDimensionalExecutor::HandleBootstrap(const std::string & identity)
{
  if (!fIpcSession || !fIpcSession->isTcp) return false;
  const size_t assignedIdx = fIpcSession->bootstrapNextIdx++;
  NLogDebug("NDimensionalExecutor::HandleBootstrap: assigning index %zu to worker '%s'", assignedIdx,
            identity.c_str());
  return NDimensionalIpcRunner::SendFrames(fIpcSession->router,
                                           {identity, "CONFIG", std::to_string(assignedIdx),
                                            fIpcSession->macroList, fIpcSession->tmpDir,
                                            fIpcSession->tmpResultsDir});
}

void NDimensionalExecutor::StartProcessIpc(std::vector<NThreadData *> & workerObjects, size_t processCount,
                                           const std::string & tcpBindEndpoint, const std::string & jobDir,
                                           const std::string & treeName, const std::string & macroList,
                                           const std::string & tmpDir, const std::string & tmpResultsDir)
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

  // For IPC we need all processesToUse; for TCP we need at least 1.
  const size_t readyTarget = isTcp ? 1 : processesToUse;

  while (fIpcSession->workerIdentityVec.size() < readyTarget) {
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

  if (!isTcp) {
    InstallIpcSigIntHandler(fIpcSession->childPids, fIpcSession->oldSigIntAction, fIpcSession->hasOldSigIntAction);
  } else {
    // TCP mode: install handler with no child PIDs — just sets gIpcSigIntRequested
    // so the dispatch loop can break cleanly and FinishProcessIpc sends STOP to workers.
    InstallIpcSigIntHandler({}, fIpcSession->oldSigIntAction, fIpcSession->hasOldSigIntAction);
  }
}

size_t NDimensionalExecutor::ExecuteCurrentBoundsProcessIpc(const std::string & definitionName,
                                                            const std::vector<Long64_t> * definitionIds,
                                                            const std::function<void(size_t, size_t)> & progressCallback)
{
  if (!fIpcSession) {
    throw std::runtime_error("IPC session is not active.");
  }
  if (fNumDimensions == 0) {
    return 0;
  }

  // Save current definition so late-joining workers can catch up
  fIpcSession->currentDefName    = definitionName;
  fIpcSession->currentDefIds     = definitionIds ? *definitionIds : std::vector<Long64_t>{};
  fIpcSession->hasCurrentDefIds  = (definitionIds != nullptr);

  if (!definitionName.empty()) {
    for (const auto & identity : fIpcSession->workerIdentityVec) {
      if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "SETDEF", definitionName})) {
        throw std::runtime_error("Failed to send IPC SETDEF message to worker '" + identity + "'.");
      }
      if (definitionIds != nullptr) {
        if (!NDimensionalIpcRunner::SendFrames(
                fIpcSession->router, {identity, "SETIDS", NDimensionalIpcRunner::SerializeIds(*definitionIds)})) {
          throw std::runtime_error("Failed to send IPC SETIDS message to worker '" + identity + "'.");
        }
      }
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
  std::set<size_t>  pendingTasks;

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

  while ((hasMore || outstanding > 0) && firstError.empty()) {
    if (isUserInterrupted()) {
      firstError = "Interrupted by user";
      break;
    }

    const size_t currentWorkers      = fIpcSession->workerIdentityVec.size();
    const size_t maxInFlightMessages = std::max<size_t>(currentWorkers * 8, 8);

    while (hasMore && outstandingMessages < maxInFlightMessages && firstError.empty()) {
      if (fIpcSession->workerIdentityVec.empty()) break; // no workers yet — wait
      const size_t      workerSlot = dispatchMessageId % fIpcSession->workerIdentityVec.size();
      const std::string identity   = fIpcSession->workerIdentityVec[workerSlot];
      std::vector<std::pair<size_t, std::string>> batchTasks;
      const size_t nw              = fIpcSession->workerIdentityVec.size();
      const size_t remainingTasks  = (nextTaskId < totalTasks) ? (totalTasks - nextTaskId) : 0;
      const size_t adaptiveBatchSize = std::max<size_t>(
          1, std::min(ipcBatchSize, std::max<size_t>(1, (remainingTasks + nw - 1) / nw)));
      batchTasks.reserve(adaptiveBatchSize);

      while (hasMore && outstanding < maxInFlightMessages * ipcBatchSize && batchTasks.size() < adaptiveBatchSize) {
        batchTasks.emplace_back(nextTaskId, NDimensionalIpcRunner::SerializeCoords(fCurrentCoords));
        pendingTasks.insert(nextTaskId);
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

      if (batchTasks.size() == 1) {
        const std::string taskId = std::to_string(batchTasks[0].first);
        const std::string coords = batchTasks[0].second;
        if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "TASK", taskId, coords})) {
          firstError = "Failed to send IPC TASK message to worker '" + identity + "'.";
          break;
        }
      }
      else {
        std::ostringstream payload;
        for (size_t i = 0; i < batchTasks.size(); ++i) {
          if (i != 0) payload << ';';
          payload << batchTasks[i].first << ':' << batchTasks[i].second;
        }
        if (!NDimensionalIpcRunner::SendFrames(fIpcSession->router, {identity, "TASKB", payload.str()})) {
          firstError = "Failed to send IPC TASKB message to worker '" + identity + "'.";
          break;
        }
      }
      ++dispatchMessageId;
      ++outstandingMessages;
    }

    if (outstanding == 0 && fIpcSession->workerIdentityVec.empty()) continue;
    if (outstanding == 0 && !hasMore) continue;

    std::vector<std::string> frames;
    if (!NDimensionalIpcRunner::ReceiveFrames(fIpcSession->router, frames)) {
      if (errno == EINTR || isUserInterrupted()) {
        firstError = "Interrupted by user";
        break;
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        firstError = "Failed to receive IPC ACK/ERR from worker.";
        break;
      }

      for (size_t i = 0; i < fIpcSession->childPids.size(); ++i) {
        int   status = 0;
        pid_t rc     = waitpid(fIpcSession->childPids[i], &status, WNOHANG);
        if (rc == fIpcSession->childPids[i]) {
          firstError = "Worker process " + std::to_string(i) + " exited unexpectedly.";
          break;
        }
      }
      continue;
    }

    if (frames.size() < 2) continue;

    // Handle late-joining TCP worker
    if (fIpcSession->isTcp && frames.size() >= 2 && frames[1] == "READY") {
      if (InitTcpWorker(frames[0])) {
        sendCatchup(frames[0]);
      }
      continue;
    }

    // Handle bootstrapping worker requesting config
    if (fIpcSession->isTcp && frames.size() >= 2 && frames[1] == "BOOTSTRAP") {
      HandleBootstrap(frames[0]);
      continue;
    }

    if (frames.size() < 3) {
      firstError = "Malformed IPC message received from worker.";
      break;
    }

    const std::string & cmd = frames[1];
    if (cmd == "ACK") {
      size_t taskId = 0;
      try {
        taskId = static_cast<size_t>(std::stoull(frames[2]));
      }
      catch (...) {
        firstError = "Malformed IPC task id received from worker.";
        break;
      }

      if (pendingTasks.find(taskId) == pendingTasks.end()) {
        firstError = "Received duplicate or unknown IPC task id " + std::to_string(taskId) + ".";
        break;
      }

      pendingTasks.erase(taskId);
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
      const size_t activeWorkersNow = std::min(fIpcSession->workerIdentityVec.size(), outstandingMessages);
      if (progressCallback) {
        progressCallback(acked, activeWorkersNow);
      }
      if (acked >= nextSchedulerLogAck) {
        NLogDebug("NDimensionalExecutor::IPC: acked=%zu/%zu activeWorkers=%zu inFlightMessages=%zu pendingTasks=%zu",
                  acked, totalTasks, activeWorkersNow, outstandingMessages, pendingTasks.size());
        nextSchedulerLogAck += 200;
      }
      continue;
    }

    if (cmd == "ACKB") {
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

        if (pendingTasks.find(ackTaskId) == pendingTasks.end()) {
          firstError = "Received duplicate or unknown IPC task id " + std::to_string(ackTaskId) + ".";
          break;
        }

        pendingTasks.erase(ackTaskId);
        if (outstanding == 0) {
          firstError = "IPC outstanding counter underflow while processing ACKB.";
          break;
        }
        --outstanding;
        ++acked;
        const size_t activeWorkersNow = std::min(fIpcSession->workerIdentityVec.size(), outstandingMessages);
        if (progressCallback) {
          progressCallback(acked, activeWorkersNow);
        }
        if (acked >= nextSchedulerLogAck) {
          NLogDebug(
              "NDimensionalExecutor::IPC: acked=%zu/%zu activeWorkers=%zu inFlightMessages=%zu pendingTasks=%zu",
              acked, totalTasks, activeWorkersNow, outstandingMessages, pendingTasks.size());
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

  if (!pendingTasks.empty()) {
    throw std::runtime_error("IPC execution finished with pending tasks still unacknowledged.");
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
        doneWorkers.insert(frames[0]);
        NLogDebug("NDimensionalExecutor::FinishProcessIpc: Worker '%s' sent DONE (%zu/%zu)", frames[0].c_str(),
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
