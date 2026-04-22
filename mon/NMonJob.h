#ifndef Ndmspc_NMonJob_H
#define Ndmspc_NMonJob_H
#include <TNamed.h>
#include "NLogger.h"

namespace Ndmspc {

///
/// \class NMonJob
///
/// \brief NMonJob object
///	\author Martin Vala <mvala@cern.ch>
///

class NMonJob : public TNamed {
  public:
  NMonJob(const char * name = "", const char * title = "");
  virtual ~NMonJob();

  virtual void Print(Option_t * option = "") const;
  bool         ParseMessage(const std::string & msg);
  std::string  GetString() const;
  json         ToJson() const;
  void         AddTask(int task);

  size_t Pending() const { return fPendingTasks.size(); }
  size_t Running() const { return fRunningTasks.size(); }
  size_t Done() const { return fDoneTasks.size(); }
  size_t Skipped() const { return fSkippedTasks.size(); }

  const std::string &               getfJobName() const { return fJobName; }
  const std::vector<unsigned int> & getfPendingTasks() const { return fPendingTasks; }
  const std::vector<unsigned int> & getfRunningTasks() const { return fRunningTasks; }
  const std::vector<unsigned int> & getfDoneTasks() const { return fDoneTasks; }
  const std::vector<unsigned int> & getfSkippedTasks() const { return fSkippedTasks; }
  const std::string                 getSlurmId() const;

  bool MoveTask(const unsigned int taskId, std::vector<unsigned int> & from, std::vector<unsigned int> & to);
  bool UpdateTask(unsigned int taskId, const std::string & action, int errorCode);
  bool IsFinished() const { return fPendingTasks.empty() && fRunningTasks.empty(); }

  private:
  std::string               fJobName;
  std::vector<unsigned int> fPendingTasks; ///< Pending tasks
  std::vector<unsigned int> fRunningTasks; ///< Running tasks
  std::vector<unsigned int> fDoneTasks;    ///< Finished tasks with success
  std::vector<unsigned int> fSkippedTasks; ///< fSkipped tasks
  std::vector<int>          fErrorCodes;
  void                      CancelJob();

  /// \cond CLASSIMP
  ClassDef(NMonJob, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
