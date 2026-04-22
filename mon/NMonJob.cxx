#include "NMonJob.h"
#include "NUtils.h"


/// \cond CLASSIMP
ClassImp(Ndmspc::NMonJob);
/// \endcond

namespace Ndmspc {
NMonJob::NMonJob(const char * name, const char * title) : TNamed(name, title)
{
  fJobName = name;
}
NMonJob::~NMonJob() {}

void NMonJob::Print(Option_t *) const
{
  ///
  /// Print NMonJob information
  ///
  NLogInfo("    %s, P[%zu] R[%zu] D[%zu] S[%zu] title: %s", fName.Data(), fPendingTasks.size(), fRunningTasks.size(),
           fDoneTasks.size(), fSkippedTasks.size(), fTitle.Data());
}

bool NMonJob::ParseMessage(const std::string & msg)
{
  ///
  /// Parse message and update job status
  ///

  if (msg.empty()) {
    NLogWarning("NMonJob::ParseMessage: Empty message received !!!");
    return false;
  }

  json j = json::parse(msg, nullptr, false);
  if (j.is_discarded()) {
    NLogWarning("NMonJob::ParseMessage: invalid JSON");
    return false;
  }

  if (j.contains("name")) {
    std::string name = j["name"];
    SetName(name.c_str());
    // fName = j["name"].get<std::string>().c_str();
  }

  if (j.contains("tasks")) {
    for (const auto & task : j["tasks"]) {
      AddTask(task.get<int>());
    }
  }

  return true;
}

void NMonJob::AddTask(int task)
{
  fPendingTasks.push_back(task);
}

std::string NMonJob::GetString() const
{
  ///
  /// Get string representation of the job status
  ///

  json        j   = ToJson();
  std::string msg = j.dump();
  // NLogInfo("NMonJob::GetString(): msg: %s", msg.c_str());

  return msg;
}

json NMonJob::ToJson() const
{
  json j;
  j["name"]    = fName;
  j["pending"] = fPendingTasks;
  j["running"] = fRunningTasks;
  j["done"]    = fDoneTasks;
  j["rc"]      = fErrorCodes;
  j["skipped"] = fSkippedTasks;
  // NLogInfo("NMonJob::ToJson(): msg: %s", j.dump().c_str());
  return j;
}

bool NMonJob::UpdateTask(unsigned int taskId, const std::string & action, int errorCode)
{
  if (action == "R" /*"start"*/) {
    if (MoveTask(taskId, fPendingTasks, fRunningTasks)) {
      return true;
    }
    return false;
  }
  else if (action == "D" /*"done"*/) {
    if (MoveTask(taskId, fRunningTasks, fDoneTasks)) {
      fErrorCodes.push_back(errorCode);
      return true;
    }
    return false;
  }
  else if (action == "S" /*"skipped"*/) {
    if (MoveTask(taskId, fPendingTasks, fSkippedTasks)) {
      return true;
    }
    return false;
  }
  else if (action == "C" /*"cancel"*/) {
    CancelJob();
    return true;
  }
  else {
    return false;
  }

  return true;
}

bool NMonJob::MoveTask(const unsigned int taskId, std::vector<unsigned int> & from, std::vector<unsigned int> & to)
{
  auto it = std::find(from.begin(), from.end(), taskId);

  if (it != from.end()) {
    to.push_back(*it);
    from.erase(it);
    return true;
  }
  else {
    NLogWarning("Task '%u' not found.", taskId);
    return false;
  }
}

void NMonJob::CancelJob()
{
  NLogInfo("Cancelling job '%s'", fName.Data());

  std::string jobId = getSlurmId();

  if (!jobId.empty()) {
    std::string cmd = "scancel " + jobId;
    NLogInfo("Executing: %s", cmd.c_str());

    int ret = system(cmd.c_str());
    NLogInfo("scancel return code: %d", ret);

    if (ret != 0) {
      NLogWarning("scancel failed for job %s", jobId.c_str());
      return;
    }

    std::vector<unsigned int> pendingCopy = fPendingTasks;
    for (unsigned int taskId : pendingCopy) {
      MoveTask(taskId, fPendingTasks, fSkippedTasks);
    }
    std::vector<unsigned int> runningCopy = fRunningTasks;
    for (unsigned int taskId : runningCopy) {
      MoveTask(taskId, fRunningTasks, fSkippedTasks);
    }
  }
}

const std::string NMonJob::getSlurmId() const
{
  std::string name = getfJobName();
  auto        pos  = name.find("_");
  if (pos == std::string::npos) {
    return "";
  }

  return name.substr(pos + 1);
}
} // namespace Ndmspc
