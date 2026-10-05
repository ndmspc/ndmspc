#include "NActionWorker.h"

#include "ndmspc/core/NLogger.h"

namespace Ndmspc {

NActionWorker::NActionWorker()
{
  fThread = std::thread([this]() { Loop(); });
  fId     = fThread.get_id();
}

NActionWorker::~NActionWorker()
{
  {
    std::lock_guard<std::mutex> lock(fMutex);
    fStopping = true;
  }
  fCv.notify_all();
  if (fThread.joinable()) {
    fThread.join();
  }
}

void NActionWorker::Submit(std::function<void()> job)
{
  {
    std::lock_guard<std::mutex> lock(fMutex);
    fJobs.push(std::move(job));
  }
  fCv.notify_one();
}

bool NActionWorker::OnWorkerThread() const
{
  return std::this_thread::get_id() == fId;
}

void NActionWorker::Loop()
{
  NLogger::SetThreadName("ActionWorker");
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(fMutex);
      fCv.wait(lock, [this]() { return fStopping || !fJobs.empty(); });
      // Shutting down: drop what is still queued rather than run it against a dying server. The job
      // already running is waited for by the destructor's join.
      if (fStopping) {
        return;
      }
      job = std::move(fJobs.front());
      fJobs.pop();
    }
    try {
      job();
    }
    catch (const std::exception & error) {
      NLogError("ActionWorker: job threw: %s", error.what());
    }
    catch (...) {
      NLogError("ActionWorker: job threw an unknown exception");
    }
  }
}

} // namespace Ndmspc
