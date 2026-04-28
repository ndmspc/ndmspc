#include <cerrno>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <zmq.h>
#include "NDimensionalIpcRunner.h"
#include "NGnThreadData.h"
#include "NThreadData.h"

namespace Ndmspc {

namespace {
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
  oss << "wk_" << std::setw(3) << std::setfill('0') << workerIndex;
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
  threadName << "ipc_" << std::setw(3) << std::setfill('0') << workerIndex;
  NLogger::SetThreadName(threadName.str());

  void * ctx = zmq_ctx_new();
  if (!ctx) return 1;

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

  bool finishedOk = true;
  while (true) {
    std::vector<std::string> frames;
    if (!ReceiveFrames(dealer, frames)) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      finishedOk = false;
      break;
    }
    if (frames.empty()) continue;

    const std::string & cmd = frames[0];
    if (cmd == "STOP") {
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
        for (const auto & task : batchTasks) {
          errTaskId = task.first;
          worker->Process(task.second);
          ackedTaskIds.push_back(task.first);
        }

        if (!SendFrames(dealer, {"ACKB", SerializeTaskIds(ackedTaskIds)})) {
          finishedOk = false;
          break;
        }
      }
    }
    catch (const std::exception & ex) {
      SendFrames(dealer, {"ERR", errTaskId.empty() ? "0" : errTaskId, ex.what()});
      finishedOk = false;
      break;
    }
    catch (...) {
      SendFrames(dealer, {"ERR", errTaskId.empty() ? "0" : errTaskId, "unknown worker exception"});
      finishedOk = false;
      break;
    }
  }

  if (auto * gnWorker = dynamic_cast<NGnThreadData *>(worker)) {
    NLogDebug("Worker %zu finished processing, executing end function and closing file if open ...", workerIndex);
    gnWorker->ExecuteEndFunction();
    if (gnWorker->GetHnSparseBase()) {
      gnWorker->GetHnSparseBase()->Close(true);
    }
  }

  zmq_close(dealer);
  zmq_ctx_term(ctx);
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
      waitpid(pid, &status, 0);
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
