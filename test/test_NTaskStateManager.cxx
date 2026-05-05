#include <gtest/gtest.h>
#include <vector>
#include "NTaskStateManager.h"

using namespace Ndmspc;

/// Unit tests for NTaskStateManager state machine without IPC complexity
class NTaskStateManagerTest : public ::testing::Test {
 protected:
  NTaskStateManager mgr;
};

/// Test adding a pending task
TEST_F(NTaskStateManagerTest, AddPendingCreatesTask) {
  ASSERT_EQ(mgr.PendingCount(), 0);
  ASSERT_EQ(mgr.RunningCount(), 0);
  ASSERT_EQ(mgr.DoneCount(), 0);
  
  std::vector<int> coords{1, 2, 3};
  mgr.AddPending(0, coords);
  
  ASSERT_EQ(mgr.PendingCount(), 1);
  ASSERT_EQ(mgr.RunningCount(), 0);
  ASSERT_EQ(mgr.DoneCount(), 0);
}

/// Test claiming a pending task atomically assigns it to a worker
TEST_F(NTaskStateManagerTest, ClaimNextPendingAtomicAssignment) {
  std::vector<int> coords{1, 2, 3};
  mgr.AddPending(0, coords);
  mgr.AddPending(1, std::vector<int>{4, 5, 6});
  
  ASSERT_EQ(mgr.PendingCount(), 2);
  
  size_t taskId;
  std::vector<int> payload;
  bool ok = mgr.ClaimNextPendingForWorker("worker_1", taskId, payload);
  
  ASSERT_TRUE(ok);
  ASSERT_EQ(mgr.PendingCount(), 1);
  ASSERT_EQ(mgr.RunningCount(), 1);
  ASSERT_EQ(taskId, 0);
  ASSERT_EQ(payload, coords);
}

/// Test marking a task as done transitions it properly
TEST_F(NTaskStateManagerTest, MarkDoneTransition) {
  std::vector<int> coords{1, 2, 3};
  mgr.AddPending(0, coords);
  
  size_t taskId;
  std::vector<int> payload;
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload);
  
  ASSERT_TRUE(mgr.MarkDone(taskId));
  ASSERT_EQ(mgr.PendingCount(), 0);
  ASSERT_EQ(mgr.RunningCount(), 0);
  ASSERT_EQ(mgr.DoneCount(), 1);
}

/// Test marking a task as done fails if it doesn't exist
TEST_F(NTaskStateManagerTest, MarkDoneFailsForUnknownTask) {
  ASSERT_FALSE(mgr.MarkDone(999)); // Non-existent task
}

/// Test marking a task as failed transitions it back to pending
TEST_F(NTaskStateManagerTest, MarkFailedRequeuesPending) {
  std::vector<int> coords{1, 2, 3};
  mgr.AddPending(0, coords);
  
  size_t taskId;
  std::vector<int> payload;
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload);
  
  ASSERT_EQ(mgr.PendingCount(), 0);
  ASSERT_EQ(mgr.RunningCount(), 1);
  
  bool ok = mgr.MarkFailed(taskId);
  ASSERT_TRUE(ok);
  ASSERT_EQ(mgr.PendingCount(), 1);
  ASSERT_EQ(mgr.RunningCount(), 0);
}

/// Test recovering all tasks from a failed worker
TEST_F(NTaskStateManagerTest, RecoverWorkerTasksReturnsAllPending) {
  // Assign tasks to worker1
  mgr.AddPending(0, std::vector<int>{1});
  mgr.AddPending(1, std::vector<int>{2});
  mgr.AddPending(2, std::vector<int>{3});
  
  size_t taskId;
  std::vector<int> payload;
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // taskId=0
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // taskId=1
  
  // Mark one as done
  mgr.MarkDone(0);
  
  // Now pending={2}, running={1}, done={0}
  ASSERT_EQ(mgr.PendingCount(), 1);
  ASSERT_EQ(mgr.RunningCount(), 1);
  
  // Recover should only get running tasks assigned to worker_1 (not done, not globally pending)
  auto recovered = mgr.RecoverWorkerTasks("worker_1");
  ASSERT_EQ(recovered.size(), 1); // Only taskId=1 (running, assigned to worker_1)
  ASSERT_EQ(recovered[0].first, 1);
  
  // After recovery, all of worker_1's tasks should be back in pending
  ASSERT_EQ(mgr.PendingCount(), 2); // task 1 + task 2 (which was already pending unassigned)
  ASSERT_EQ(mgr.RunningCount(), 0);
  ASSERT_EQ(mgr.DoneCount(), 1); // task 0 stays done
}

/// Test recovering from a worker with no tasks
TEST_F(NTaskStateManagerTest, RecoverWorkerWithNoTasksReturnsEmpty) {
  mgr.AddPending(0, std::vector<int>{1});
  
  auto recovered = mgr.RecoverWorkerTasks("nonexistent_worker");
  ASSERT_EQ(recovered.size(), 0);
  ASSERT_EQ(mgr.PendingCount(), 1); // Still pending
}

/// Test requeuing a task
TEST_F(NTaskStateManagerTest, RequeueTaskMovesToPending) {
  std::vector<int> coords{1, 2, 3};
  mgr.AddPending(0, coords);
  
  size_t taskId;
  std::vector<int> payload;
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload);
  mgr.MarkDone(taskId);
  
  ASSERT_EQ(mgr.DoneCount(), 1);
  
  bool ok = mgr.RequeueTask(taskId);
  ASSERT_TRUE(ok);
  ASSERT_EQ(mgr.PendingCount(), 1);
  ASSERT_EQ(mgr.DoneCount(), 0);
}

/// Test that only pending & running tasks are recovered (done tasks stay done)
TEST_F(NTaskStateManagerTest, RecoverDoesNotAffectDoneTasks) {
  mgr.AddPending(0, std::vector<int>{1});
  mgr.AddPending(1, std::vector<int>{2});
  mgr.AddPending(2, std::vector<int>{3});
  
  size_t taskId;
  std::vector<int> payload;
  
  // Assign 0,1 to worker_1; 2 remains pending
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // 0
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // 1
  
  // Mark 0 as done
  mgr.MarkDone(0);
  
  // Recover should only get running task 1, not done task 0
  auto recovered = mgr.RecoverWorkerTasks("worker_1");
  ASSERT_EQ(recovered.size(), 1);
  ASSERT_EQ(recovered[0].first, 1); // Only running task
  ASSERT_EQ(mgr.DoneCount(), 1); // task 0 still done
}

/// Test multiple workers don't interfere with each other
TEST_F(NTaskStateManagerTest, MultipleWorkersIndependent) {
  mgr.AddPending(0, std::vector<int>{1});
  mgr.AddPending(1, std::vector<int>{2});
  mgr.AddPending(2, std::vector<int>{3});
  
  size_t taskId;
  std::vector<int> payload;
  
  // Assign to worker_1
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // 0
  
  // Assign to worker_2
  mgr.ClaimNextPendingForWorker("worker_2", taskId, payload); // 1
  
  // Recover worker_1 should not affect worker_2's tasks
  auto recovered1 = mgr.RecoverWorkerTasks("worker_1");
  ASSERT_EQ(recovered1.size(), 1);
  ASSERT_EQ(mgr.RunningCount(), 1); // worker_2's task still running
  
  // Task 1 should still be running (assigned to worker_2)
  auto recovered2 = mgr.RecoverWorkerTasks("worker_2");
  ASSERT_EQ(recovered2.size(), 1);
  ASSERT_EQ(recovered2[0].first, 1);
}

/// Test GetWorkerTasks returns tasks assigned to a worker
TEST_F(NTaskStateManagerTest, GetWorkerTasksReturnsAssignedTasks) {
  mgr.AddPending(0, std::vector<int>{1});
  mgr.AddPending(1, std::vector<int>{2});
  mgr.AddPending(2, std::vector<int>{3});
  
  size_t taskId;
  std::vector<int> payload;
  
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // 0
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload); // 1
  
  auto tasks = mgr.GetWorkerTasks("worker_1");
  ASSERT_EQ(tasks.size(), 2);
  
  // task 2 should not be in worker_1's list (still pending, not assigned)
  auto tasks2 = mgr.GetWorkerTasks("worker_2");
  ASSERT_EQ(tasks2.size(), 0);
}

/// Test clear resets all state
TEST_F(NTaskStateManagerTest, ClearResetsAllState) {
  mgr.AddPending(0, std::vector<int>{1});
  mgr.AddPending(1, std::vector<int>{2});
  
  size_t taskId;
  std::vector<int> payload;
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload);
  mgr.MarkDone(0);
  
  mgr.Clear();
  
  ASSERT_EQ(mgr.PendingCount(), 0);
  ASSERT_EQ(mgr.RunningCount(), 0);
  ASSERT_EQ(mgr.DoneCount(), 0);
}

/// Test IsDone check
TEST_F(NTaskStateManagerTest, IsDoneCorrectlyIdentifiesDoneTasks) {
  mgr.AddPending(0, std::vector<int>{1});
  
  size_t taskId;
  std::vector<int> payload;
  mgr.ClaimNextPendingForWorker("worker_1", taskId, payload);
  
  ASSERT_FALSE(mgr.IsDone(0));
  mgr.MarkDone(0);
  ASSERT_TRUE(mgr.IsDone(0));
}

/// Test stress: many tasks and workers
TEST_F(NTaskStateManagerTest, StressManyTasksAndWorkers) {
  const size_t numTasks = 1000;
  const size_t numWorkers = 10;
  
  // Add all tasks as pending
  for (size_t i = 0; i < numTasks; ++i) {
    mgr.AddPending(i, std::vector<int>{static_cast<int>(i)});
  }
  ASSERT_EQ(mgr.PendingCount(), numTasks);
  
  // Distribute tasks to workers
  size_t taskId;
  std::vector<int> payload;
  for (size_t i = 0; i < numTasks; ++i) {
    const std::string workerId = "worker_" + std::to_string(i % numWorkers);
    bool ok = mgr.ClaimNextPendingForWorker(workerId, taskId, payload);
    ASSERT_TRUE(ok);
  }
  ASSERT_EQ(mgr.PendingCount(), 0);
  ASSERT_EQ(mgr.RunningCount(), numTasks);
  
  // Mark half as done
  for (size_t i = 0; i < numTasks / 2; ++i) {
    mgr.MarkDone(i);
  }
  ASSERT_EQ(mgr.DoneCount(), numTasks / 2);
  
  // Recover all workers
  for (size_t w = 0; w < numWorkers; ++w) {
    const std::string workerId = "worker_" + std::to_string(w);
    auto recovered = mgr.RecoverWorkerTasks(workerId);
    // Each worker should have roughly numTasks/numWorkers running tasks
    ASSERT_GT(recovered.size(), 0);
  }
  
  // All should be back in pending
  ASSERT_EQ(mgr.PendingCount(), numTasks / 2); // Only running tasks recovered
}
