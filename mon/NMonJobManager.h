#ifndef Ndmspc_NMonJobs_H
#define Ndmspc_NMonJobs_H
#include <TNamed.h>
#include "NMonJob.h"
#include <nlohmann/json.hpp>

namespace Ndmspc {
using json = nlohmann::json;

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

  void        AddJob(NMonJob * job);
  json        ToJson() const;
  std::string GetString() const;
  bool        UpdateTask(const std::string & jobName, unsigned int taskId, const std::string & action);

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
