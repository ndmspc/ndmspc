#ifndef N_TASK_STATE_MANAGER_H
#define N_TASK_STATE_MANAGER_H

#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstddef>
#include <string>
#include <utility>

namespace Ndmspc {

/**
 * @class NTaskStateManager
 * @brief Manages task lifecycle: pending → running → done/failed
 * 
 * Separates task state management from IPC/worker concerns.
 * Tracks which tasks are assigned to which workers and handles redistribution.
 */
class NTaskStateManager {
  public:
  /// @brief Unique identifier type for tasks
  using TaskId = size_t;
  /// @brief Worker identifier type (typically identity string)
  using WorkerId = std::string;
  /// @brief Payload associated with a task (coordinates for NDMSPC)
  using TaskPayload = std::vector<int>;
  
  NTaskStateManager() = default;
  ~NTaskStateManager() = default;
  
  /**
   * @brief Add a new task to the pending queue
   * @param id Unique task identifier
   * @param payload Task data (typically coordinates)
   */
  void AddPending(TaskId id, const TaskPayload & payload);
  
  /**
   * @brief Assign a pending task to a worker (transitions to running)
   * @param worker Worker identifier
   * @param id Task ID to assign
   * @return true if assignment succeeded, false if task not in pending
   */
  bool AssignToWorker(const WorkerId & worker, TaskId id);

  /**
   * @brief Atomically pop the next pending task and assign it to a worker.
   * @param worker Worker identifier
   * @param id Output task ID
   * @param payload Output task payload
   * @return true if a task was claimed, false if no pending tasks exist
   */
  bool ClaimNextPendingForWorker(const WorkerId & worker, TaskId & id, TaskPayload & payload);
  
  /**
   * @brief Mark a running task as completed
   * @param id Task ID to mark done
   * @return true if task was running, false otherwise
   */
  bool MarkDone(TaskId id);
  
  /**
   * @brief Mark a running task as failed (returns to pending for redistribution)
   * @param id Task ID that failed
   * @return true if task was running, false otherwise
   */
  bool MarkFailed(TaskId id);

  /**
   * @brief Requeue a task to pending state from running or done state.
   * @param id Task ID to requeue
   * @return true if task was requeued, false if task was already pending or unknown
   */
  bool RequeueTask(TaskId id);
  
  /**
   * @brief Get all tasks currently assigned to a worker
   * @param worker Worker identifier
   * @return Set of task IDs assigned to this worker
   */
  std::set<TaskId> GetWorkerTasks(const WorkerId & worker) const;
  
  /**
   * @brief Get the next pending task for dispatch
   * @return Pair of (TaskId, TaskPayload), or invalid if none pending
   * @note This does NOT assign the task; use AssignToWorker after sending
   */
  std::pair<TaskId, TaskPayload> GetNextPending();
  
  /**
   * @brief Check if there are pending tasks
   * @return true if pending queue is non-empty
   */
  bool HasPending() const;
  
  /**
   * @brief Recover all tasks from a failed worker
   * @param worker Worker identifier
   * @return Vector of (TaskId, TaskPayload) pairs for redistribution
   * @note Tasks are moved back to pending state
   */
  std::vector<std::pair<TaskId, TaskPayload>> RecoverWorkerTasks(const WorkerId & worker);
  
  /**
   * @brief Remove a specific task ID from a worker (e.g., after ACK but worker later fails)
   * @param worker Worker identifier
   * @param id Task ID to remove
   * @return true if task was found and removed, false otherwise
   */
  bool RemoveTaskFromWorker(const WorkerId & worker, TaskId id);
  
  /**
   * @brief Get the worker currently executing a task
   * @param id Task ID
   * @return Worker ID if task is running, empty string if not running
   */
  WorkerId GetTaskWorker(TaskId id) const;
  
  /**
   * @brief Check if a task has been completed
   * @param id Task ID
   * @return true if task is in done set
   */
  bool IsDone(TaskId id) const;
  
  /// Get number of pending tasks
  size_t PendingCount() const { return fPending.size(); }
  
  /// Get number of running tasks
  size_t RunningCount() const { return fRunning.size(); }
  
  /// Get number of completed tasks
  size_t DoneCount() const { return fDone.size(); }
  
  /// Get total tasks tracked (pending + running + done)
  size_t TotalCount() const { return PendingCount() + RunningCount() + DoneCount(); }
  
  /// Clear all state (for reuse or cleanup)
  void Clear();
  
  private:
  // State buckets: tasks flow pending → running → done
  std::queue<std::pair<TaskId, TaskPayload>> fPending;  ///< Pending tasks not yet dispatched
  std::unordered_set<TaskId>                fPendingIds; ///< Index of pending task ids
  std::unordered_map<TaskId, TaskPayload>   fRunning;   ///< Tasks currently assigned to workers
  std::set<TaskId>                          fDone;      ///< Tasks that have completed successfully
  
  // Mappings for efficient lookup
  std::unordered_map<WorkerId, std::set<TaskId>> fWorkerToTasks;  ///< Current assignments per worker
  std::unordered_map<TaskId, WorkerId>           fTaskToWorker;   ///< Reverse mapping task->worker
  std::unordered_map<TaskId, TaskPayload>        fTaskPayloads;   ///< Stored payloads for recovery
  
  /// Check if task exists in any state
  bool TaskExists(TaskId id) const;

  /// Push task into pending queue and pending-id index.
  void EnqueuePending(TaskId id, const TaskPayload & payload);
};

} // namespace Ndmspc

#endif // N_TASK_STATE_MANAGER_H
