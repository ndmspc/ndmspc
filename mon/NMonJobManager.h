#ifndef Ndmspc_NMonJobManager_H
#define Ndmspc_NMonJobManager_H
#include <TNamed.h>
#include "ndmspc/core/NLogger.h"
#include "ndmspc/mon/NMonJob.h"


namespace Ndmspc {


///
/// \class NMonJobManager
///
/// \brief NMonJobManager object
///	\author Martin Vala <mvala@cern.ch>
///

class NMonJobManager : public TNamed {
  public:
  /**
   * @brief Constructor.
   * @param name Object name (default: "NMonJobManager").
   * @param title Object title (default: "Mon Job Manager").
   */
  NMonJobManager(const char * name = "NMonJobManager", const char * title = "Mon Job Manager");
  virtual ~NMonJobManager();

  /**
   * @brief Print the managed jobs.
   * @param option Optional ROOT option string (unused).
   */
  void Print(Option_t * option = "") const override;

  /// @brief Get the map of managed jobs by name.
  std::map<std::string, NMonJob *> getfJobs() { return fJobs; }

  /**
   * @brief Add a job to the manager.
   * @param job Job to add (ownership is taken by the manager).
   * @return True when the job was added.
   */
  bool        AddJob(NMonJob * job);
  /**
   * @brief Serialize all managed jobs to JSON.
   * @return JSON array of job statuses.
   */
  json        ToJson() const;
  /**
   * @brief Get the string representation of all managed jobs.
   * @return Serialized JSON.
   */
  std::string GetString() const;
  /**
   * @brief Apply an action to a task of a named job.
   * @param jobName Name of the job.
   * @param taskId Task id.
   * @param action Action code ("R" start, "D" done, "S" skipped, "C" cancel).
   * @param errorCode Return code recorded when the task completes.
   * @return True when the job was found and the action applied.
   */
  bool        UpdateTask(const std::string & jobName, unsigned int taskId, const std::string & action, int errorCode);
  /**
   * @brief Delete a job by name.
   * @param jobName Name of the job to delete.
   * @return True when the job was found and deleted.
   */
  bool        DeleteJob(const std::string & jobName);
  /**
   * @brief Delete a job by pointer.
   * @param job Job to delete.
   * @return True when the job was found and deleted.
   */
  bool        DeleteJob(NMonJob * job);
  /// @brief Delete all jobs that have finished.
  void        ClearFinishedJobs();

  private:
  std::map<std::string, NMonJob *> fJobs; ///< Managed jobs by name

  /// \cond CLASSIMP
  ClassDefOverride(NMonJobManager, 1);
  /// \endcond;
  ///
  ///
};
} // namespace Ndmspc
#endif
