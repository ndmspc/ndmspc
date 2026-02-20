#include "NMonJob.h"
#include "NUtils.h"
#include "NLogger.h"

using json = nlohmann::json;

/// \cond CLASSIMP
ClassImp(Ndmspc::NMonJob);
/// \endcond

namespace Ndmspc {
NMonJob::NMonJob(const char * name, const char * title) : TNamed(name, title)
{
  fJobName = name;
}
NMonJob::~NMonJob() {}

void NMonJob::Print(Option_t * option) const
{
  ///
  /// Print NMonJob information
  ///

  NLogInfo("NMonJob: %s, title: %s P[%zu]", fName.Data(), fTitle.Data(), fPendingTasks.size());
  NLogInfo(" Running tasks: %zu", fRunningTasks.size());
  NLogInfo("    Done tasks: %zu", fDoneTasks.size());
  NLogInfo("  Failed tasks: %zu", fFailedTasks.size());
  NLogInfo(" Skipped tasks: %zu", fSkippedTasks.size());
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
    fName = j["name"].get<std::string>().c_str();
  }

  for (const auto & task : j["tasks"]) {
    AddTask(task.get<int>());
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
  j["failed"]  = fFailedTasks;
  j["skipped"] = fSkippedTasks;
  // NLogInfo("NMonJob::ToJson(): msg: %s", j.dump().c_str());
  return j;
}

bool NMonJob::UpdateTask(unsigned int taskId, const std::string & action)
{
  if (action == "start") {
    MoveTask(taskId, fPendingTasks, fRunningTasks);
  }
  else if (action == "done") {
    MoveTask(taskId, fRunningTasks, fDoneTasks);
  }
  else if (action == "failed") {
    MoveTask(taskId, fRunningTasks, fFailedTasks);
  }
  else if (action == "skipped") {
    MoveTask(taskId, fPendingTasks, fSkippedTasks);
  }
  else {
    return false;
  }

  return true;
}

void NMonJob::MoveTask(const unsigned int taskId, std::vector<unsigned int> & from, std::vector<unsigned int> & to)
{
  auto it = std::find(from.begin(), from.end(), taskId);

  if (it != from.end()) {
    to.push_back(*it);
    from.erase(it);
  }
  else {
    NLogWarning("Task '%u' not found.", taskId);
  }
}

} // namespace Ndmspc
