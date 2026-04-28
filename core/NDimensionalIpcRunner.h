#ifndef Ndmspc_NDimensionalIpcRunner_H
#define Ndmspc_NDimensionalIpcRunner_H

#include <string>
#include <vector>
#include <sys/types.h>
#include <Rtypes.h>

namespace Ndmspc {

class NThreadData;

class NDimensionalIpcRunner {
  public:
  static bool SendFrames(void * socket, const std::vector<std::string> & frames);
  static bool ReceiveFrames(void * socket, std::vector<std::string> & outFrames);

  static std::string BuildWorkerIdentity(size_t workerIndex);
  static std::string SerializeCoords(const std::vector<int> & coords);
  static std::string SerializeIds(const std::vector<Long64_t> & ids);

  static int  WorkerLoop(const std::string & endpoint, size_t workerIndex, NThreadData * worker);
  static bool WaitForChildProcesses(const std::vector<pid_t> & pids, int timeoutMs = -1);
  static void CleanupChildProcesses(const std::vector<pid_t> & pids);

  private:
  static std::vector<int> ParseCoords(const std::string & coordsStr);
  static std::vector<Long64_t> ParseIds(const std::string & idsStr);
};

} // namespace Ndmspc

#endif
