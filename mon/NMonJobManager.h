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
  NMonJobManager(const char * name = "NMonJobManager", const char * title = "Mon Job Manager");
  virtual ~NMonJobManager();

  void Print(Option_t * option = "") const override;

  std::map<std::string, NMonJob *> getfJobs() { return fJobs; }

  bool        AddJob(NMonJob * job);
  json        ToJson() const;
  std::string GetString() const;
  bool        UpdateTask(const std::string & jobName, unsigned int taskId, const std::string & action, int errorCode);
  bool        DeleteJob(const std::string & jobName);
  bool        DeleteJob(NMonJob * job);
  void        ClearFinishedJobs();

  private:
  std::map<std::string, NMonJob *> fJobs;

  /// \cond CLASSIMP
  ClassDefOverride(NMonJobManager, 1);
  /// \endcond;
  ///
  ///
};
} // namespace Ndmspc
#endif
