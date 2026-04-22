#include "NMonJobManager.h"
#include "NUtils.h"


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
      // NLogInfo("    %s", p.first.c_str());
      p.second->Print(option);
    }
  }
}

bool NMonJobManager::AddJob(NMonJob * job)
{
  std::string name = job->GetName();
  if (name.empty()) {
    return false;
  }
  fJobs[name] = job;
  NLogInfo("NMonJobManager::AddJob(job): job: %s", job->GetName());
  return true;
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

bool NMonJobManager::DeleteJob(const std::string & jobName)
{
  auto element = fJobs.find(jobName);

  if (element == fJobs.end()) {
    NLogWarning("Job '%s' not found", jobName.c_str());
    return false;
  }

  delete element->second;
  fJobs.erase(element);

  NLogInfo("Job '%s' removed", jobName.c_str());
  return true;
}

bool NMonJobManager::DeleteJob(NMonJob * job)
{
  if (!job) return false;
  return DeleteJob(job->GetName());
}

void NMonJobManager::ClearFinishedJobs()
{
  for (auto it = fJobs.begin(); it != fJobs.end();) {
    if (it->second->IsFinished()) {
      NLogInfo("Removing finished job '%s'", it->first.c_str());
      delete it->second;
      it = fJobs.erase(it);
    }
    else {
      ++it;
    }
  }
}

} // namespace Ndmspc
