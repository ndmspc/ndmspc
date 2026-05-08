#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <netinet/in.h>

#include <TSystem.h>

#include "06_binnings/NBinnings01Gaus.C"
#include "NGnTree.h"

namespace {

std::string gExecutablePath;

class ScopedEnv {
  public:
  explicit ScopedEnv(const std::string & key) : fKey(key)
  {
    const char * val = gSystem->Getenv(fKey.c_str());
    if (val) {
      fHadValue = true;
      fOldValue = val;
    }
  }

  ~ScopedEnv()
  {
    if (fHadValue)
      gSystem->Setenv(fKey.c_str(), fOldValue.c_str());
    else
      gSystem->Unsetenv(fKey.c_str());
  }

  private:
  std::string fKey;
  bool        fHadValue{false};
  std::string fOldValue;
};

bool ParseBoolEnv(const char * value)
{
  if (!value) return false;
  std::string v(value);
  for (char & c : v) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return (v == "1" || v == "true" || v == "yes" || v == "on");
}

int ReserveFreeTcpPort()
{
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(0);

  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
    ::close(fd);
    return -1;
  }

  const int port = static_cast<int>(ntohs(addr.sin_port));
  ::close(fd);
  return port;
}

pid_t SpawnTcpWorker(const std::string & endpoint, size_t workerIndex, const std::string & outFile, bool onlyOddPoints)
{
  pid_t pid = ::fork();
  if (pid != 0) return pid;

  const std::string idx = std::to_string(workerIndex);
  ::execl(gExecutablePath.c_str(), gExecutablePath.c_str(), "--ndmspc-tcp-worker", endpoint.c_str(), idx.c_str(),
          outFile.c_str(), onlyOddPoints ? "1" : "0", nullptr);

  _exit(127);
}

void CleanupWorkers(const std::vector<pid_t> & pids)
{
  for (pid_t pid : pids) {
    if (pid <= 0) continue;
    int   status = 0;
    pid_t rc     = ::waitpid(pid, &status, WNOHANG);
    if (rc == 0) {
      ::kill(pid, SIGTERM);
      ::waitpid(pid, &status, 0);
    }
  }
}

bool RunAsTcpWorkerIfRequested(int argc, char ** argv)
{
  if (argc < 6) return false;
  if (std::string(argv[1]) != "--ndmspc-tcp-worker") return false;

  gSystem->Setenv("NDMSPC_WORKER_ENDPOINT", argv[2]);
  gSystem->Setenv("NDMSPC_WORKER_INDEX", argv[3]);
  gSystem->Setenv("ROOT_MAX_THREADS", "1");
  gSystem->Setenv("NDMSPC_WORKER_TIMEOUT", "10");
  gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT", "20");

  const std::string outFileBasename = argv[4];
  const bool        onlyOddPoints   = ParseBoolEnv(argv[5]);
  const std::string workerDir       = std::string(".ndmspc-tcp-workers/") + argv[3];
  gSystem->mkdir(workerDir.c_str(), true);

  const std::string outFile = workerDir + "/" + outFileBasename;
  NBinnings01Gaus(outFile, onlyOddPoints);
  return true;
}

} // namespace

TEST(NBinnings01GausModesTest, ValidatesScenarioFromEnvironment)
{
  const char * scenarioEnv = gSystem->Getenv("NDMSPC_TEST_SCENARIO");
  ASSERT_TRUE(scenarioEnv != nullptr) << "NDMSPC_TEST_SCENARIO must be set";
  const std::string scenario = scenarioEnv;

  const bool onlyOddPoints = ParseBoolEnv(gSystem->Getenv("NDMSPC_TEST_ONLY_ODD"));

  ScopedEnv scopedMode("NDMSPC_EXECUTION_MODE");
  ScopedEnv scopedNProc("NDMSPC_MAX_PROCESSES");
  ScopedEnv scopedThreads("ROOT_MAX_THREADS");
  ScopedEnv scopedTcpPort("NDMSPC_TCP_PORT");
  ScopedEnv scopedWorkerTimeout("NDMSPC_WORKER_TIMEOUT");
  ScopedEnv scopedIpcStallTimeout("NDMSPC_IPC_STALL_TIMEOUT");

  std::vector<pid_t> workerPids;
  const std::string  testFile = "test_NBinnings01Gaus_" + scenario + (onlyOddPoints ? "_odd" : "_all") + ".root";

  if (scenario == "thread1") {
    gSystem->Setenv("NDMSPC_EXECUTION_MODE", "thread");
    gSystem->Setenv("ROOT_MAX_THREADS", "1");
  }
  else if (scenario == "thread4") {
    gSystem->Setenv("NDMSPC_EXECUTION_MODE", "thread");
    gSystem->Setenv("ROOT_MAX_THREADS", "4");
  }
  else if (scenario == "ipc4") {
    gSystem->Setenv("NDMSPC_EXECUTION_MODE", "ipc");
    gSystem->Setenv("ROOT_MAX_THREADS", "4");
    gSystem->Setenv("NDMSPC_MAX_PROCESSES", "4");
  }
  else if (scenario == "tcp4") {
    gSystem->Setenv("NDMSPC_EXECUTION_MODE", "tcp");
    gSystem->Setenv("ROOT_MAX_THREADS", "4");
    gSystem->Setenv("NDMSPC_MAX_PROCESSES", "4");
    gSystem->Setenv("NDMSPC_WORKER_TIMEOUT", "10");
    gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT", "20");

    const int port = ReserveFreeTcpPort();
    ASSERT_GT(port, 0) << "Failed to reserve a free TCP port";
    gSystem->Setenv("NDMSPC_TCP_PORT", std::to_string(port).c_str());

    const std::string endpoint = "tcp://127.0.0.1:" + std::to_string(port);
    for (size_t i = 0; i < 4; ++i) {
      const pid_t pid = SpawnTcpWorker(endpoint, i, testFile, onlyOddPoints);
      ASSERT_GT(pid, 0) << "Failed to spawn TCP worker " << i;
      workerPids.push_back(pid);
    }
  }
  else {
    FAIL() << "Unknown NDMSPC_TEST_SCENARIO='" << scenario << "'";
  }

  std::remove(testFile.c_str());

  NBinnings01Gaus(testFile, onlyOddPoints);

  std::ifstream out(testFile);
  ASSERT_TRUE(out.good()) << "Output file was not created";
  out.close();

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt != nullptr);
  ASSERT_FALSE(ngnt->IsZombie());

  const Long64_t nEntries = ngnt->GetEntries();
  ASSERT_GT(nEntries, 0) << "No entries were produced";

  Long64_t nEntriesContent = ngnt->GetBinning()->GetContent()->GetNbins();
  if (onlyOddPoints) {
    nEntriesContent  /= 2;
  }

  ASSERT_GT(nEntriesContent, 0) << "No entries were produced";

  // lets test if nEntries matches the number of entries in the content
  ASSERT_EQ(nEntries, nEntriesContent)
      << "Number of entries in the tree does not match the number of entries in the binning content";

  ngnt->Close();

  CleanupWorkers(workerPids);
  std::remove(testFile.c_str());
}

int main(int argc, char ** argv)
{
  if (argc > 0) gExecutablePath = argv[0];

  if (RunAsTcpWorkerIfRequested(argc, argv)) return 0;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
