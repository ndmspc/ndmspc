#include "NMonJobManager.h"
#include "NUtils.h"
#include "NLogger.h"
using json = nlohmann::json;

/// \cond CLASSIMP
ClassImp(Ndmspc::NMonJobManager);
/// \endcond

namespace Ndmspc {
NMonJobManager::NMonJobManager(const char * name, const char * title) : TNamed(name, title) {}
NMonJobManager::~NMonJobManager()
{
  for (auto & p : fJobs) {
    delete p.second;
  }
  fJobs.clear();
}

void NMonJobManager::Print(Option_t * option) const
{
  NLogInfo("NMonJobManager name=%s title=%s", fName.Data(), fTitle.Data());
  if (fJobs.empty()) {
    NLogInfo("  No jobs available.");
    return;
  }
  else {
    NLogInfo("  Number of jobs: %zu", fJobs.size());
    NLogInfo("  Jobs:");
    for (const auto & p : fJobs) {
      //NLogInfo("    %s", p.first.c_str());
      p.second->Print(option);
    }
  }
}

void NMonJobManager::AddJob(NMonJob * job)
{
  fJobs[job->GetName()] = job;
  NLogInfo("NMonJobManager::AddJob(job): job: %s", job->GetName());
}

json NMonJobManager::ToJson() const
{
  json j = json::array();
  for (const auto & [id, job] : fJobs) {
    j.push_back(job->ToJson());
  }
  return j;
}

std::string NMonJobManager::GetString() const
{
  json        j   = ToJson();
  std::string msg = j.dump();

  return msg;
}

bool NMonJobManager::UpdateTask(const std::string & jobName, unsigned int taskId, const std::string & action,
                                int errorCode)
{
  NLogTrace("NMonJobManager::UpdateTask(jobName=%s, taskId=%u, action=%s, errorCode=%d)", jobName.c_str(), taskId,
            action.c_str(), errorCode);
  if (fJobs.find(jobName) == fJobs.end()) {
    NLogWarning("Job with name %s not found in UpdateTask", jobName.c_str());
    return false;
  }
  return fJobs[jobName]->UpdateTask(taskId, action, errorCode); // fixme treba odhandlovat invalidne vstupy
}

} // namespace Ndmspc
