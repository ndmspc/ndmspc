#include "NTaskStateManager.h"
#include <stdexcept>

namespace Ndmspc {

void NTaskStateManager::AddPending(TaskId id, const TaskPayload & payload)
{
  if (TaskExists(id)) {
    throw std::runtime_error("Task " + std::to_string(id) + " already exists in state manager");
  }
  EnqueuePending(id, payload);
  fTaskPayloads[id] = payload;
}

bool NTaskStateManager::AssignToWorker(const WorkerId & worker, TaskId id)
{
  if (fPending.empty() || fPendingIds.find(id) == fPendingIds.end()) {
    return false;
  }

  // Find and remove from pending
  std::queue<std::pair<TaskId, TaskPayload>> tempQueue;
  bool found = false;
  
  while (!fPending.empty()) {
    auto [taskId, payload] = std::move(fPending.front());
    fPending.pop();
    
    if (taskId == id && !found) {
      found = true;
      // Move to running state
      fRunning[id] = payload;
      fWorkerToTasks[worker].insert(id);
      fTaskToWorker[id] = worker;
      fPendingIds.erase(id);
    } else {
      tempQueue.emplace(taskId, payload);
    }
  }
  
  fPending = std::move(tempQueue);
  return found;
}

bool NTaskStateManager::ClaimNextPendingForWorker(const WorkerId & worker, TaskId & id, TaskPayload & payload)
{
  if (fPending.empty()) {
    return false;
  }

  auto item = std::move(fPending.front());
  fPending.pop();
  fPendingIds.erase(item.first);

  id      = item.first;
  payload = std::move(item.second);

  fRunning[id] = payload;
  fWorkerToTasks[worker].insert(id);
  fTaskToWorker[id] = worker;
  return true;
}

bool NTaskStateManager::MarkDone(TaskId id)
{
  auto it = fRunning.find(id);
  if (it == fRunning.end()) {
    return false; // Task not running
  }
  
  // Move from running to done
  fDone.insert(id);
  fRunning.erase(it);
  
  // Remove from worker assignment
  auto workerIt = fTaskToWorker.find(id);
  if (workerIt != fTaskToWorker.end()) {
    const WorkerId & worker = workerIt->second;
    fWorkerToTasks[worker].erase(id);
    if (fWorkerToTasks[worker].empty()) {
      fWorkerToTasks.erase(worker);
    }
    fTaskToWorker.erase(workerIt);
  }
  
  return true;
}

bool NTaskStateManager::MarkFailed(TaskId id)
{
  auto it = fRunning.find(id);
  if (it == fRunning.end()) {
    return false; // Task not running
  }
  
  // Move from running back to pending
  TaskPayload payload = it->second;
  fRunning.erase(it);
  EnqueuePending(id, payload);
  
  // Remove from worker assignment
  auto workerIt = fTaskToWorker.find(id);
  if (workerIt != fTaskToWorker.end()) {
    const WorkerId & worker = workerIt->second;
    fWorkerToTasks[worker].erase(id);
    if (fWorkerToTasks[worker].empty()) {
      fWorkerToTasks.erase(worker);
    }
    fTaskToWorker.erase(workerIt);
  }
  
  return true;
}

bool NTaskStateManager::RequeueTask(TaskId id)
{
  // Running -> pending
  auto runningIt = fRunning.find(id);
  if (runningIt != fRunning.end()) {
    TaskPayload payload = runningIt->second;
    fRunning.erase(runningIt);
    EnqueuePending(id, payload);

    auto workerIt = fTaskToWorker.find(id);
    if (workerIt != fTaskToWorker.end()) {
      const WorkerId & worker = workerIt->second;
      auto wkIt = fWorkerToTasks.find(worker);
      if (wkIt != fWorkerToTasks.end()) {
        wkIt->second.erase(id);
        if (wkIt->second.empty()) {
          fWorkerToTasks.erase(wkIt);
        }
      }
      fTaskToWorker.erase(workerIt);
    }
    return true;
  }

  // Done -> pending
  auto doneIt = fDone.find(id);
  if (doneIt != fDone.end()) {
    auto payloadIt = fTaskPayloads.find(id);
    if (payloadIt == fTaskPayloads.end()) {
      return false;
    }
    fDone.erase(doneIt);
    EnqueuePending(id, payloadIt->second);
    return true;
  }

  // Unknown or already pending.
  return false;
}

std::set<NTaskStateManager::TaskId> NTaskStateManager::GetWorkerTasks(const WorkerId & worker) const
{
  auto it = fWorkerToTasks.find(worker);
  if (it != fWorkerToTasks.end()) {
    return it->second;
  }
  return {};
}

std::pair<NTaskStateManager::TaskId, NTaskStateManager::TaskPayload> NTaskStateManager::GetNextPending()
{
  if (fPending.empty()) {
    return {0, {}};
  }
  
  auto [taskId, payload] = fPending.front();
  return {taskId, payload};
}

bool NTaskStateManager::HasPending() const
{
  return !fPending.empty();
}

std::vector<std::pair<NTaskStateManager::TaskId, NTaskStateManager::TaskPayload>>
NTaskStateManager::RecoverWorkerTasks(const WorkerId & worker)
{
  std::vector<std::pair<TaskId, TaskPayload>> recovered;
  
  auto workerIt = fWorkerToTasks.find(worker);
  if (workerIt == fWorkerToTasks.end()) {
    return recovered; // No tasks assigned to this worker
  }
  
  const auto & taskIds = workerIt->second;
  for (TaskId id : taskIds) {
    auto runningIt = fRunning.find(id);
    if (runningIt != fRunning.end()) {
      // Move back to pending
      const TaskPayload & payload = runningIt->second;
      recovered.emplace_back(id, payload);
      EnqueuePending(id, payload);
      fRunning.erase(runningIt);
    }
    fTaskToWorker.erase(id);
  }
  
  fWorkerToTasks.erase(workerIt);
  return recovered;
}

bool NTaskStateManager::RemoveTaskFromWorker(const WorkerId & worker, TaskId id)
{
  auto workerIt = fWorkerToTasks.find(worker);
  if (workerIt == fWorkerToTasks.end()) {
    return false;
  }
  
  size_t erased = workerIt->second.erase(id);
  if (erased > 0) {
    if (workerIt->second.empty()) {
      fWorkerToTasks.erase(workerIt);
    }
    fTaskToWorker.erase(id);
    return true;
  }
  
  return false;
}

NTaskStateManager::WorkerId NTaskStateManager::GetTaskWorker(TaskId id) const
{
  auto it = fTaskToWorker.find(id);
  if (it != fTaskToWorker.end()) {
    return it->second;
  }
  return {};
}

bool NTaskStateManager::IsDone(TaskId id) const
{
  return fDone.find(id) != fDone.end();
}

void NTaskStateManager::Clear()
{
  fPending = std::queue<std::pair<TaskId, TaskPayload>>();
  fPendingIds.clear();
  fRunning.clear();
  fDone.clear();
  fWorkerToTasks.clear();
  fTaskToWorker.clear();
  fTaskPayloads.clear();
}

bool NTaskStateManager::TaskExists(TaskId id) const
{
  return fPendingIds.find(id) != fPendingIds.end() ||
         fRunning.find(id) != fRunning.end() ||
         fDone.find(id) != fDone.end() ||
         fTaskPayloads.find(id) != fTaskPayloads.end();
}

void NTaskStateManager::EnqueuePending(TaskId id, const TaskPayload & payload)
{
  if (fPendingIds.insert(id).second) {
    fPending.emplace(id, payload);
  }
}

} // namespace Ndmspc
