#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <zmq.h>
#include "NUtils.h"
#include <TSystem.h>
#include "NDimensionalIpcRunner.h"
#include "NGnThreadData.h"
#include "NThreadData.h"

namespace Ndmspc {

namespace {
// Worker signal handler for Ctrl+C
volatile sig_atomic_t gWorkerInterrupted = 0;

void WorkerSigIntHandler(int)
{
  gWorkerInterrupted = 1;
}

std::string SerializeTaskIds(const std::vector<std::string> & taskIds)
{
  std::ostringstream oss;
  for (size_t i = 0; i < taskIds.size(); ++i) {
    if (i != 0) oss << ',';
    oss << taskIds[i];
  }
  return oss.str();
}

bool ParseTaskBatchPayload(const std::string & payload, std::vector<std::pair<std::string, std::vector<int>>> & tasks)
{
  tasks.clear();
  std::stringstream batchStream(payload);
  std::string       taskToken;
  while (std::getline(batchStream, taskToken, ';')) {
    if (taskToken.empty()) continue;
    size_t sep = taskToken.find(':');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= taskToken.size()) {
      return false;
    }
    const std::string taskId   = taskToken.substr(0, sep);
    const std::string coords   = taskToken.substr(sep + 1);
    std::vector<int>  parsedCoords;
    std::stringstream coordStream(coords);
    std::string       coordToken;
    while (std::getline(coordStream, coordToken, ',')) {
      if (coordToken.empty()) continue;
      parsedCoords.push_back(std::stoi(coordToken));
    }
    tasks.emplace_back(taskId, std::move(parsedCoords));
  }
  return !tasks.empty();
}
} // namespace

bool NDimensionalIpcRunner::SendFrames(void * socket, const std::vector<std::string> & frames)
{
  for (size_t i = 0; i < frames.size(); ++i) {
    int flags = (i + 1 < frames.size()) ? ZMQ_SNDMORE : 0;
    if (zmq_send(socket, frames[i].data(), frames[i].size(), flags) < 0) {
      return false;
    }
  }
  return true;
}

bool NDimensionalIpcRunner::ReceiveFrames(void * socket, std::vector<std::string> & outFrames)
{
  outFrames.clear();
  while (true) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    int rc = zmq_msg_recv(&msg, socket, 0);
    if (rc < 0) {
      zmq_msg_close(&msg);
      return false;
    }
    outFrames.emplace_back(static_cast<const char *>(zmq_msg_data(&msg)), static_cast<size_t>(rc));
    int more = zmq_msg_more(&msg);
    zmq_msg_close(&msg);
    if (!more) break;
  }
  return true;
}

std::string NDimensionalIpcRunner::BuildWorkerIdentity(size_t workerIndex)
{
  std::ostringstream oss;
  oss << "wk_" << std::setw(6) << std::setfill('0') << workerIndex;
  return oss.str();
}

std::string NDimensionalIpcRunner::SerializeCoords(const std::vector<int> & coords)
{
  std::ostringstream oss;
  for (size_t i = 0; i < coords.size(); ++i) {
    if (i != 0) oss << ',';
    oss << coords[i];
  }
  return oss.str();
}

std::string NDimensionalIpcRunner::SerializeIds(const std::vector<Long64_t> & ids)
{
  std::ostringstream oss;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) oss << ',';
    oss << ids[i];
  }
  return oss.str();
}

int NDimensionalIpcRunner::WorkerLoop(const std::string & endpoint, size_t workerIndex, NThreadData * worker)
{
  std::ostringstream threadName;
  threadName << "ipc_" << std::setw(6) << std::setfill('0') << workerIndex;
  NLogger::SetThreadName(threadName.str());

  void * ctx = zmq_ctx_new();
  if (!ctx) {
    return 1;
  }

  void * dealer = zmq_socket(ctx, ZMQ_DEALER);
  if (!dealer) {
    zmq_ctx_term(ctx);
    return 1;
  }

  const std::string identity = BuildWorkerIdentity(workerIndex);
  if (zmq_setsockopt(dealer, ZMQ_IDENTITY, identity.data(), identity.size()) != 0) {
    zmq_close(dealer);
    zmq_ctx_term(ctx);
    return 1;
  }

  int timeoutMs = 1000;
  zmq_setsockopt(dealer, ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));

  if (zmq_connect(dealer, endpoint.c_str()) != 0) {
    zmq_close(dealer);
    zmq_ctx_term(ctx);
    return 1;
  }

  if (!SendFrames(dealer, {"READY"})) {
    zmq_close(dealer);
    zmq_ctx_term(ctx);
    return 1;
  }

  NLogPrint("Worker %zu: connected to %s, ready for tasks", workerIndex, endpoint.c_str());

  int rc = TaskLoop(dealer, workerIndex, worker);

  zmq_close(dealer);
  zmq_ctx_term(ctx);
  return rc;
}

int NDimensionalIpcRunner::TaskLoop(void * dealer, size_t workerIndex, NThreadData * worker)
{
  // Install signal handler for Ctrl+C
  gWorkerInterrupted = 0;
  struct sigaction sa;
  struct sigaction oldSa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = WorkerSigIntHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, &oldSa);

  bool   finishedOk    = true;
  bool   aborted       = false;
  bool   wasInterrupted = false;
  size_t tasksProcessed = 0;
  size_t lastReportedProgress = 0;
  const bool showWorkerProgress = []() {
    const char * env = gSystem->Getenv("NDMSPC_WORKER_PROGRESS");
    if (!env || env[0] == '\0') return false;
    std::string value(env);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (value == "1" || value == "true" || value == "yes" || value == "on");
  }();
  
  const size_t progressReportInterval = []() -> size_t {
    const char * env = gSystem->Getenv("NDMSPC_WORKER_PROGRESS_INTERVAL");
    if (!env || env[0] == '\0') return 50UL; // default: report every 50 tasks
    try {
      int val = std::stoi(env);
      return (val > 0) ? static_cast<size_t>(val) : 50UL;
    } catch (...) {
      return 50UL;
    }
  }();

  // Non-blocking check: poll the socket and consume a STOP frame if present.
  // Used between sub-tasks in a batch to react to supervisor abort quickly.
  auto checkAbort = [&]() -> bool {
    // Check for Ctrl+C first
    if (gWorkerInterrupted) {
      if (!aborted) {
        // Send shutdown notification on first detection
        SendFrames(dealer, {"SHUTDOWN", "interrupted", std::to_string(tasksProcessed)});
      }
      aborted = true;
      return true;
    }
    zmq_pollitem_t item = {dealer, 0, ZMQ_POLLIN, 0};
    if (zmq_poll(&item, 1, 0) <= 0) return false;
    std::vector<std::string> peek;
    if (!ReceiveFrames(dealer, peek)) return false;
    if (!peek.empty() && peek[0] == "STOP") {
      aborted = (peek.size() >= 2 && peek[1] == "abort");
      if (aborted) NLogPrint("Worker %zu: received abort from supervisor, stopping ...", workerIndex);
      return true;
    }
    return false; // unexpected frame — ignore, will be handled in main loop
  };

  while (true) {
    // Check for Ctrl+C
    if (gWorkerInterrupted) {
      if (showWorkerProgress && tasksProcessed > 0) { NLogPrint(""); }
      NLogPrint("Worker %zu: interrupted by user (Ctrl+C), exiting ...", workerIndex);
      NLogPrint("Worker %zu: interrupted by user (Ctrl+C) after processing %zu tasks, shutting down...", workerIndex, tasksProcessed);
      // Notify supervisor before exiting
      SendFrames(dealer, {"SHUTDOWN", "interrupted", std::to_string(tasksProcessed)});
      aborted = true;
      wasInterrupted = true;
      finishedOk = false;
      break;
    }

    std::vector<std::string> frames;
    if (!ReceiveFrames(dealer, frames)) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Check for interruption on timeout
        if (gWorkerInterrupted) {
          if (showWorkerProgress && tasksProcessed > 0) { NLogPrint(""); }
          NLogPrint("Worker %zu: interrupted by user (Ctrl+C), exiting ...", workerIndex);
          NLogPrint("Worker %zu: interrupted by user (Ctrl+C) after processing %zu tasks, shutting down...", workerIndex, tasksProcessed);
          // Notify supervisor before exiting
          SendFrames(dealer, {"SHUTDOWN", "interrupted", std::to_string(tasksProcessed)});
          aborted = true;
          wasInterrupted = true;
          finishedOk = false;
          break;
        }
        continue;
      }
      finishedOk = false;
      break;
    }
    if (frames.empty()) continue;

    const std::string & cmd = frames[0];
    if (cmd == "STOP") {
      if (showWorkerProgress && tasksProcessed > 0) { NLogPrint(""); }
      aborted = (frames.size() >= 2 && frames[1] == "abort");
      if (aborted) {
        NLogPrint("Worker %zu: received abort from supervisor, stopping ...", workerIndex);
      } else {
        // NLogPrint("Worker %zu: received STOP, processed %zu tasks total", workerIndex, tasksProcessed);
      }
      break;
    }
    if (cmd == "SETDEF") {
      if (frames.size() < 2) {
        finishedOk = false;
        break;
      }
      if (auto * gnWorker = dynamic_cast<NGnThreadData *>(worker)) {
        gnWorker->SetCurrentDefinitionName(frames[1]);
        continue;
      }
      finishedOk = false;
      break;
    }
    if (cmd == "SETIDS") {
      if (frames.size() < 2) {
        finishedOk = false;
        break;
      }
      if (auto * gnWorker = dynamic_cast<NGnThreadData *>(worker)) {
        gnWorker->SyncCurrentDefinitionIds(ParseIds(frames[1]));
        continue;
      }
      finishedOk = false;
      break;
    }
    if (cmd != "TASK" && cmd != "TASKB") {
      finishedOk = false;
      break;
    }

    std::string errTaskId;
    try {
      if (cmd == "TASK") {
        if (frames.size() < 3) {
          finishedOk = false;
          break;
        }

        const std::string taskId = frames[1];
        errTaskId = taskId;
        std::vector<int> coords = ParseCoords(frames[2]);
        ++tasksProcessed;
        if (showWorkerProgress) {
          NLogPrint("Worker %zu: processing tasks [done: %zu]", workerIndex, tasksProcessed);
        }
        // Report progress at configured interval for visibility
        // Always show first task to confirm worker is actively processing
        if (tasksProcessed == 1 || tasksProcessed - lastReportedProgress >= progressReportInterval) {
          NLogPrint("Worker %zu: processed %zu tasks", workerIndex, tasksProcessed);
          lastReportedProgress = tasksProcessed;
        }
        worker->Process(coords);
        if (!SendFrames(dealer, {"ACK", taskId})) {
          finishedOk = false;
          break;
        }
      }
      else {
        if (frames.size() < 2) {
          finishedOk = false;
          break;
        }

        std::vector<std::pair<std::string, std::vector<int>>> batchTasks;
        if (!ParseTaskBatchPayload(frames[1], batchTasks)) {
          finishedOk = false;
          break;
        }

        std::vector<std::string> ackedTaskIds;
        ackedTaskIds.reserve(batchTasks.size());
        tasksProcessed += batchTasks.size();
        if (showWorkerProgress) {
          NLogPrint("Worker %zu: processing tasks [done: %zu]", workerIndex, tasksProcessed);
        }
        // Report progress at configured interval for visibility
        // Always show first task to confirm worker is actively processing
        if (tasksProcessed == 1 || tasksProcessed - lastReportedProgress >= progressReportInterval) {
          NLogPrint("Worker %zu: processed %zu tasks", workerIndex, tasksProcessed);
          lastReportedProgress = tasksProcessed;
        }
        for (const auto & task : batchTasks) {
          if (checkAbort()) { finishedOk = false; break; }
          errTaskId = task.first;
          worker->Process(task.second);
          ackedTaskIds.push_back(task.first);
        }
        if (aborted) break;

        if (!SendFrames(dealer, {"ACKB", SerializeTaskIds(ackedTaskIds)})) {
          finishedOk = false;
          break;
        }
      }
    }
    catch (const std::exception & ex) {
      NLogPrint("Worker %zu: ERROR processing task %s: %s", workerIndex, errTaskId.c_str(), ex.what());
      SendFrames(dealer, {"ERR", errTaskId.empty() ? "0" : errTaskId, ex.what()});
      finishedOk = false;
      break;
    }
    catch (...) {
      NLogPrint("Worker %zu: ERROR processing task %s: unknown exception", workerIndex, errTaskId.c_str());
      SendFrames(dealer, {"ERR", errTaskId.empty() ? "0" : errTaskId, "unknown worker exception"});
      finishedOk = false;
      break;
    }
  }

  if (auto * gnWorker = dynamic_cast<NGnThreadData *>(worker)) {
    // Capture local tmp file path before closing (storage object path is cleared after Close).
    std::string localTmpFile;
    if (gnWorker->GetHnSparseBase() && gnWorker->GetHnSparseBase()->GetStorageTree()) {
      localTmpFile = gnWorker->GetHnSparseBase()->GetStorageTree()->GetFileName();
    }

    if (aborted) {
      // Supervisor aborted — skip end function and copy, but close the file handle
      // so we can delete it cleanly.
      NLogPrint("Worker %zu: aborting, skipping post-processing.", workerIndex);
      if (gnWorker->GetHnSparseBase()) {
        gnWorker->GetHnSparseBase()->Close(false); // false = don't save
      }
    } else {
      NLogDebug("Worker %zu finished processing, executing end function and closing file if open ...", workerIndex);
      gnWorker->ExecuteEndFunction();
      if (gnWorker->GetHnSparseBase()) {
        gnWorker->GetHnSparseBase()->Close(true);
      }

      // TCP mode: copy local result file to shared results dir so supervisor can merge.
      const std::string & resultsFilename = gnWorker->GetResultsFilename();
      if (!resultsFilename.empty()) {
        if (!localTmpFile.empty() && localTmpFile != resultsFilename) {
          const std::string resultsDir = std::string(gSystem->GetDirName(resultsFilename.c_str()));
          NUtils::CreateDirectory(resultsDir);
          NLogPrint("Worker %zu copying '%s' -> '%s' ...", workerIndex, localTmpFile.c_str(), resultsFilename.c_str());
          if (!NUtils::Cp(localTmpFile, resultsFilename, kFALSE)) {
            NLogError("Worker %zu: failed to copy '%s' to '%s'", workerIndex, localTmpFile.c_str(),
                      resultsFilename.c_str());
          }
        }
      }
    }

    // Delete the local tmp file only when a distinct results file was set.
    // An empty resultsFilename means localTmpFile IS the results file (same path),
    // so it must be kept for the supervisor to merge.
    const std::string & resultsFilenameForDelete = gnWorker->GetResultsFilename();
    const bool hasDistinctResultsFile = !resultsFilenameForDelete.empty() && resultsFilenameForDelete != localTmpFile;
    if (!localTmpFile.empty() && hasDistinctResultsFile) {
      NLogPrint("Worker %zu: removing local tmp file '%s'", workerIndex, localTmpFile.c_str());
      gSystem->Unlink(localTmpFile.c_str());
    }
  }

  if (!aborted) {
    // Signal master that this worker has finished writing its file.
    // For TCP mode, master waits for this DONE before starting to merge.
    // For IPC (fork) mode, master uses WaitForChildProcesses instead; the DONE
    // message stays unread in the ZMQ buffer which is harmless.
    SendFrames(dealer, {"DONE"});
    NLogPrint("Worker %zu: completed successfully, processed %zu tasks total", workerIndex, tasksProcessed);
  } else if (wasInterrupted) {
    // Interrupted by Ctrl+C - already printed message above, don't print duplicate
  } else if (!finishedOk) {
    NLogPrint("Worker %zu: exited with error after processing %zu tasks", workerIndex, tasksProcessed);
  }

  // Drop any unsent/undelivered messages immediately so zmq_close/zmq_ctx_term
  // do not hang at shutdown.
  int linger = 0;
  zmq_setsockopt(dealer, ZMQ_LINGER, &linger, sizeof(linger));

  // Restore original signal handler
  sigaction(SIGINT, &oldSa, nullptr);

  return finishedOk ? 0 : 1;
}

bool NDimensionalIpcRunner::WaitForChildProcesses(const std::vector<pid_t> & pids, int timeoutMs)
{
  bool allExitedCleanly = true;
  const bool useTimeout = (timeoutMs >= 0);

  for (pid_t pid : pids) {
    if (pid <= 0) continue;

    int status = 0;
    pid_t rc = -1;
    if (!useTimeout) {
      rc = waitpid(pid, &status, 0);
    }
    else {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
      while (std::chrono::steady_clock::now() < deadline) {
        rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid || rc < 0) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (rc == 0) {
        allExitedCleanly = false;
        continue;
      }
    }

    if (rc < 0) {
      allExitedCleanly = false;
      continue;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      allExitedCleanly = false;
    }
  }
  return allExitedCleanly;
}

void NDimensionalIpcRunner::CleanupChildProcesses(const std::vector<pid_t> & pids)
{
  for (pid_t pid : pids) {
    if (pid <= 0) continue;
    int status = 0;
    pid_t rc = waitpid(pid, &status, WNOHANG);
    if (rc == pid) {
      continue;
    }
    if (rc < 0) {
      continue;
    }

    kill(pid, SIGTERM);
    rc = waitpid(pid, &status, WNOHANG);
    if (rc == pid || rc < 0) {
      continue;
    }

    if (!WaitForChildProcesses({pid}, 1500)) {
      kill(pid, SIGKILL);

      // Never block indefinitely here: if a child is stuck in uninterruptible I/O
      // state, waitpid(..., 0) can hang forever and stall process shutdown.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
      while (std::chrono::steady_clock::now() < deadline) {
        rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid || rc < 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (rc == 0) {
        NLogWarning("NDimensionalIpcRunner::CleanupChildProcesses: child pid=%d did not exit after SIGKILL; continuing shutdown", pid);
      }
    }
  }
}

std::vector<int> NDimensionalIpcRunner::ParseCoords(const std::string & coordsStr)
{
  std::vector<int> coords;
  std::stringstream ss(coordsStr);
  std::string       token;
  while (std::getline(ss, token, ',')) {
    if (token.empty()) continue;
    coords.push_back(std::stoi(token));
  }
  return coords;
}

std::vector<Long64_t> NDimensionalIpcRunner::ParseIds(const std::string & idsStr)
{
  std::vector<Long64_t> ids;
  std::stringstream     ss(idsStr);
  std::string           token;
  while (std::getline(ss, token, ',')) {
    if (token.empty()) continue;
    ids.push_back(static_cast<Long64_t>(std::stoll(token)));
  }
  return ids;
}

} // namespace Ndmspc
