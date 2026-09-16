#ifndef Ndmspc_NMonJob_H
#define Ndmspc_NMonJob_H
#include <TNamed.h>
#include "ndmspc/core/NLogger.h"

namespace Ndmspc {

///
/// \class NMonJob
///
/// \brief NMonJob object
///	\author Martin Vala <mvala@cern.ch>
///

class NMonJob : public TNamed {
  public:
  /**
   * @brief Constructor.
   * @param name Job name (also used to derive the Slurm id).
   * @param title Job title.
   */
  NMonJob(const char * name = "", const char * title = "");
  virtual ~NMonJob();

  /**
   * @brief Print the job status.
   * @param option Optional ROOT option string (unused).
   */
  virtual void Print(Option_t * option = "") const;
  /**
   * @brief Parse a status message and update the job.
   * @param msg JSON status message.
   * @return True when the message was parsed.
   */
  bool         ParseMessage(const std::string & msg);
  /**
   * @brief Get the string representation of the job status.
   * @return Serialized JSON status.
   */
  std::string  GetString() const;
  /**
   * @brief Serialize the job status to JSON.
   * @return JSON object with name, task lists and return codes.
   */
  json         ToJson() const;
  /**
   * @brief Add a task to the pending list.
   * @param task Task id.
   */
  void         AddTask(int task);

  /// @brief Get the number of pending tasks.
  size_t Pending() const { return fPendingTasks.size(); }
  /// @brief Get the number of running tasks.
  size_t Running() const { return fRunningTasks.size(); }
  /// @brief Get the number of successfully finished tasks.
  size_t Done() const { return fDoneTasks.size(); }
  /// @brief Get the number of skipped tasks.
  size_t Skipped() const { return fSkippedTasks.size(); }

  /// @brief Get the job name.
  const std::string &               getfJobName() const { return fJobName; }
  /// @brief Get the pending task ids.
  const std::vector<unsigned int> & getfPendingTasks() const { return fPendingTasks; }
  /// @brief Get the running task ids.
  const std::vector<unsigned int> & getfRunningTasks() const { return fRunningTasks; }
  /// @brief Get the finished task ids.
  const std::vector<unsigned int> & getfDoneTasks() const { return fDoneTasks; }
  /// @brief Get the skipped task ids.
  const std::vector<unsigned int> & getfSkippedTasks() const { return fSkippedTasks; }
  /// @brief Get the Slurm job id derived from the job name ("" when absent).
  const std::string                 getSlurmId() const;

  /**
   * @brief Move a task between two task lists.
   * @param taskId Task id to move.
   * @param from Source list.
   * @param to Destination list.
   * @return True when the task was found in the source list.
   */
  bool MoveTask(const unsigned int taskId, std::vector<unsigned int> & from, std::vector<unsigned int> & to);
  /**
   * @brief Apply an action to a task and update its list.
   * @param taskId Task id.
   * @param action Action code ("R" start, "D" done, "S" skipped, "C" cancel).
   * @param errorCode Return code recorded when the task completes.
   * @return True when the action was applied.
   */
  bool UpdateTask(unsigned int taskId, const std::string & action, int errorCode);
  /// @brief Whether no pending or running tasks remain.
  bool IsFinished() const { return fPendingTasks.empty() && fRunningTasks.empty(); }

  private:
  std::string               fJobName;      ///< Job name (also used to derive the Slurm id)
  std::vector<unsigned int> fPendingTasks; ///< Pending tasks
  std::vector<unsigned int> fRunningTasks; ///< Running tasks
  std::vector<unsigned int> fDoneTasks;    ///< Finished tasks with success
  std::vector<unsigned int> fSkippedTasks; ///< fSkipped tasks
  std::vector<int>          fErrorCodes;   ///< Return codes of finished tasks

  /// @brief Skip all pending and running tasks.
  void                      CancelJob();

  /// \cond CLASSIMP
  ClassDef(NMonJob, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
