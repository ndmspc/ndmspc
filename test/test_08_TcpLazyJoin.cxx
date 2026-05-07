#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <TMacro.h>
#include <TSystem.h>

#include "NGnTree.h"
#include "NUtils.h"

namespace {

std::string gExecutablePath;

std::string ResolveNTcpTestMacroPath()
{
  const char * envMacro = gSystem->Getenv("NDMSPC_TCP_TEST_MACRO");
  if (envMacro && envMacro[0] != '\0' && ::access(envMacro, R_OK) == 0)
    return envMacro;

  const std::vector<std::string> candidates = {
    "./NTcpTest.C",
    gSystem->DirName(gExecutablePath.c_str()) + std::string("/NTcpTest.C"),
  };

  for (const auto & path : candidates) {
    if (::access(path.c_str(), R_OK) == 0) return path;
  }
  return "";
}

std::string ResolveCliBinary(const std::string & name)
{
  const std::string exeDir = gSystem->DirName(gExecutablePath.c_str());
  const std::vector<std::string> candidates = {
    exeDir + std::string("/../core/cli/") + name,
    exeDir + std::string("/") + name,
    name,
  };

  for (const auto & path : candidates) {
    if (::access(path.c_str(), X_OK) == 0) return path;
  }
  return "";
}

Long64_t ExpectedEntriesFromBins(const std::string & bins)
{
  std::stringstream ss(bins);
  std::string       item;
  Long64_t          total = 1;
  while (std::getline(ss, item, ',')) {
    total *= std::stoll(item);
  }
  return total;
}

bool ExecuteNTcpTestMacro(const std::string & outFile, int timeoutMs, std::string * error = nullptr,
                          const std::string & bins = "10,10,5,5")
{
  const std::string macroPath = ResolveNTcpTestMacroPath();
  if (macroPath.empty()) {
    if (error) *error = "Could not locate NTcpTest.C (set NDMSPC_TCP_TEST_MACRO or ensure NTcpTest.C is in test run dir)";
    return false;
  }

  TMacro * macro = Ndmspc::NUtils::OpenMacro(macroPath);
  if (!macro) {
    if (error) *error = "Failed to open macro file: " + macroPath;
    return false;
  }

  std::ostringstream params;
  params << '"' << outFile << "\",\"" << bins << "\"," << timeoutMs;
  macro->Exec(params.str().c_str());
  delete macro;
  return true;
}

// ── helpers ────────────────────────────────────────────────────────────────

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

std::vector<pid_t> GetChildPids(pid_t parentPid)
{
  std::vector<pid_t> children;
#ifdef __linux__
  const std::string path = "/proc/" + std::to_string(parentPid) + "/task/" + std::to_string(parentPid) + "/children";
  std::ifstream in(path);
  pid_t childPid = 0;
  while (in >> childPid) {
    if (childPid > 0) children.push_back(childPid);
  }
#else
  (void)parentPid;
#endif
  return children;
}

// Spawn this binary as a TCP worker.
// argv layout (worker re-entry):
// exe --ndmspc-tcp-worker endpoint idx outfile timeout_ms bins [startup_delay_ms]
pid_t SpawnTcpWorker(const std::string & endpoint, size_t idx, const std::string & outFile,
                     int timeoutMs, const std::string & bins = "10,10,5,5", int startupDelayMs = 0)
{
  const pid_t pid = ::fork();
  if (pid != 0) return pid;

  ::execl(gExecutablePath.c_str(), gExecutablePath.c_str(),
          "--ndmspc-tcp-worker",
          endpoint.c_str(),
          std::to_string(idx).c_str(),
          outFile.c_str(),
          std::to_string(timeoutMs).c_str(),
          bins.c_str(),
          std::to_string(startupDelayMs).c_str(),
          nullptr);
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

// Re-entry point: when this binary is invoked as a TCP worker, run NTcpTest
// in worker mode (NDMSPC_WORKER_ENDPOINT drives TCP dispatch).
bool RunAsTcpWorkerIfRequested(int argc, char ** argv)
{
  // Expected: exe --ndmspc-tcp-worker endpoint idx outfile timeout_ms bins [startup_delay_ms]
  if (argc < 7) return false;
  if (std::string(argv[1]) != "--ndmspc-tcp-worker") return false;

  const std::string bins = argv[6];
  const int startupDelayMs = (argc >= 8) ? std::atoi(argv[7]) : 0;
  if (startupDelayMs > 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(startupDelayMs));

  gSystem->Setenv("NDMSPC_WORKER_ENDPOINT", argv[2]);
  gSystem->Setenv("NDMSPC_WORKER_INDEX",    argv[3]);
  gSystem->Setenv("ROOT_MAX_THREADS",       "1");
  gSystem->Setenv("NDMSPC_WORKER_TIMEOUT",      "10");
  gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT",   "20");

  const std::string workerDir = std::string(".ndmspc-tcp-workers/") + argv[3];
  gSystem->mkdir(workerDir.c_str(), true);

  const std::string outFile   = workerDir + "/" + argv[4];
  const int         timeoutMs = std::atoi(argv[5]);
  std::string error;
  if (!ExecuteNTcpTestMacro(outFile, timeoutMs, &error, bins)) {
    NLogPrint("worker failed to execute NTcpTest.C: %s", error.c_str());
    _exit(2);
  }
  return true;
}

class ScopedEnv {
public:
  explicit ScopedEnv(const std::string & key) : fKey(key)
  {
    const char * val = gSystem->Getenv(fKey.c_str());
    if (val) { fHadValue = true; fOldValue = val; }
  }
  ~ScopedEnv()
  {
    if (fHadValue) gSystem->Setenv(fKey.c_str(), fOldValue.c_str());
    else           gSystem->Unsetenv(fKey.c_str());
  }
private:
  std::string fKey, fOldValue;
  bool        fHadValue{false};
};

constexpr Long64_t kExpectedNTcpTestEntries = 10 * 10 * 5 * 5;
constexpr const char * kInterruptTestBins   = "8,8,4,4";

void AssertOutputTreeIsValid(const std::string & testFile,
                             Long64_t            expectedEntries = kExpectedNTcpTestEntries)
{
  std::ifstream out(testFile);
  EXPECT_TRUE(out.good()) << "Output file was not created: " << testFile;
  out.close();

  if (!out.good()) return;

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt != nullptr);
  ASSERT_FALSE(ngnt->IsZombie());
  EXPECT_EQ(ngnt->GetEntries(), expectedEntries)
      << "Unexpected number of processed points in output tree";
  ASSERT_TRUE(ngnt->GetBinning() != nullptr);
  ASSERT_TRUE(ngnt->GetBinning()->GetContent() != nullptr);
  EXPECT_EQ(ngnt->GetBinning()->GetContent()->GetNbins(), expectedEntries)
      << "Binning content does not contain the full expected point set";
  ngnt->Close();
}

} // namespace

// ── test: supervisor starts, workers join after 2 seconds ─────────────────

TEST(TcpLazyJoinTest, WorkersJoinAfterSupervisorStart)
{
  const int port = ReserveFreeTcpPort();
  ASSERT_GT(port, 0) << "Failed to reserve a free TCP port";
  const std::string endpoint = "tcp://127.0.0.1:" + std::to_string(port);
  const std::string testFile = "test_NTcpTest_lazy_join.root";
  std::remove(testFile.c_str());

  ScopedEnv scopedMode("NDMSPC_EXECUTION_MODE");
  ScopedEnv scopedNProc("NDMSPC_MAX_PROCESSES");
  ScopedEnv scopedThreads("ROOT_MAX_THREADS");
  ScopedEnv scopedPort("NDMSPC_TCP_PORT");
  ScopedEnv scopedWorkerTimeout("NDMSPC_WORKER_TIMEOUT");
  ScopedEnv scopedStallTimeout("NDMSPC_IPC_STALL_TIMEOUT");

  gSystem->Setenv("NDMSPC_EXECUTION_MODE",    "tcp");
  gSystem->Setenv("ROOT_MAX_THREADS",          "4");
  gSystem->Setenv("NDMSPC_MAX_PROCESSES",      "4");
  gSystem->Setenv("NDMSPC_TCP_PORT",           std::to_string(port).c_str());
  gSystem->Setenv("NDMSPC_WORKER_TIMEOUT",     "10");
  gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT",  "30");

  // Workers are spawned with a 2-second startup delay so the supervisor
  // starts listening before any worker connects (lazy join).
  const int        perPointMs  = 10;
  const int        lazyDelayMs = 2000;
  std::vector<pid_t> workerPids;
  for (size_t i = 0; i < 4; ++i) {
    const pid_t pid = SpawnTcpWorker(endpoint, i, testFile, perPointMs, "10,10,5,5", lazyDelayMs);
    ASSERT_GT(pid, 0) << "Failed to spawn TCP worker " << i;
    workerPids.push_back(pid);
  }

  std::string error;
  ASSERT_TRUE(ExecuteNTcpTestMacro(testFile, perPointMs, &error)) << error;

  AssertOutputTreeIsValid(testFile);

  CleanupWorkers(workerPids);
  std::remove(testFile.c_str());
}

// ── test: workers start first, supervisor starts after 2 seconds ──────────

TEST(TcpLazyJoinTest, SupervisorStartsAfterWorkers)
{
  const int port = ReserveFreeTcpPort();
  ASSERT_GT(port, 0) << "Failed to reserve a free TCP port";
  const std::string endpoint = "tcp://127.0.0.1:" + std::to_string(port);
  const std::string testFile = "test_NTcpTest_workers_first.root";
  std::remove(testFile.c_str());

  ScopedEnv scopedMode("NDMSPC_EXECUTION_MODE");
  ScopedEnv scopedNProc("NDMSPC_MAX_PROCESSES");
  ScopedEnv scopedThreads("ROOT_MAX_THREADS");
  ScopedEnv scopedPort("NDMSPC_TCP_PORT");
  ScopedEnv scopedWorkerTimeout("NDMSPC_WORKER_TIMEOUT");
  ScopedEnv scopedStallTimeout("NDMSPC_IPC_STALL_TIMEOUT");

  gSystem->Setenv("NDMSPC_EXECUTION_MODE",    "tcp");
  gSystem->Setenv("ROOT_MAX_THREADS",          "4");
  gSystem->Setenv("NDMSPC_MAX_PROCESSES",      "4");
  gSystem->Setenv("NDMSPC_TCP_PORT",           std::to_string(port).c_str());
  gSystem->Setenv("NDMSPC_WORKER_TIMEOUT",     "10");
  gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT",  "30");

  const int        perPointMs = 10;
  std::vector<pid_t> workerPids;
  for (size_t i = 0; i < 4; ++i) {
    const pid_t pid = SpawnTcpWorker(endpoint, i, testFile, perPointMs);
    ASSERT_GT(pid, 0) << "Failed to spawn TCP worker " << i;
    workerPids.push_back(pid);
  }

  // Workers bootstrap first and wait until supervisor appears.
  std::this_thread::sleep_for(std::chrono::seconds(2));
  std::string error;
  ASSERT_TRUE(ExecuteNTcpTestMacro(testFile, perPointMs, &error)) << error;

  AssertOutputTreeIsValid(testFile);

  CleanupWorkers(workerPids);
  std::remove(testFile.c_str());
}

// ── test: worker interruption and task redistribution ─────────────────────

TEST(TcpLazyJoinTest, WorkerInterruptionRedistributesTasks)
{
  const int port = ReserveFreeTcpPort();
  ASSERT_GT(port, 0) << "Failed to reserve a free TCP port";
  const std::string endpoint = "tcp://127.0.0.1:" + std::to_string(port);
  const std::string testFile = "test_NTcpTest_worker_interrupt.root";
  std::remove(testFile.c_str());

  ScopedEnv scopedMode("NDMSPC_EXECUTION_MODE");
  ScopedEnv scopedNProc("NDMSPC_MAX_PROCESSES");
  ScopedEnv scopedThreads("ROOT_MAX_THREADS");
  ScopedEnv scopedPort("NDMSPC_TCP_PORT");
  ScopedEnv scopedWorkerTimeout("NDMSPC_WORKER_TIMEOUT");
  ScopedEnv scopedStallTimeout("NDMSPC_IPC_STALL_TIMEOUT");

  gSystem->Setenv("NDMSPC_EXECUTION_MODE",    "tcp");
  gSystem->Setenv("ROOT_MAX_THREADS",          "2");
  gSystem->Setenv("NDMSPC_MAX_PROCESSES",      "2");
  gSystem->Setenv("NDMSPC_TCP_PORT",           std::to_string(port).c_str());
  gSystem->Setenv("NDMSPC_WORKER_TIMEOUT",     "5");   // Shorter timeout to detect disconnect faster
  gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT",  "10");  // Shorter stall timeout

  const int        perPointMs = 10;
  std::vector<pid_t> workerPids;
  
  // Spawn 2 workers
  for (size_t i = 0; i < 2; ++i) {
    const pid_t pid = SpawnTcpWorker(endpoint, i, testFile, perPointMs, kInterruptTestBins);
    ASSERT_GT(pid, 0) << "Failed to spawn TCP worker " << i;
    workerPids.push_back(pid);
  }

  // Start supervisor in a separate thread with exception handling
  std::atomic<bool> supervisorCompleted{false};
  std::atomic<bool> supervisorFailed{false};
  std::string supervisorError;
  
  std::thread supervisorThread([&]() {
    try {
      std::string error;
      bool success = ExecuteNTcpTestMacro(testFile, perPointMs, &error, kInterruptTestBins);
      if (!success) {
        supervisorError = error;
        supervisorFailed = true;
      }
      supervisorCompleted = true;
    } catch (const std::exception& e) {
      supervisorError = std::string("Exception: ") + e.what();
      supervisorFailed = true;
      supervisorCompleted = true;
    } catch (...) {
      supervisorError = "Unknown exception";
      supervisorFailed = true;
      supervisorCompleted = true;
    }
  });

  // Let processing start (give workers time to claim some tasks)
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Interrupt the first worker and require the remaining worker to replay its work.
  ASSERT_GT(workerPids[0], 0);
  ::kill(workerPids[0], SIGINT);
  ::waitpid(workerPids[0], nullptr, 0);
  workerPids[0] = -1; // Mark as killed

  // Wait for supervisor to complete (remaining worker should pick up redistributed tasks)
  supervisorThread.join();

  // Cleanup remaining worker
  CleanupWorkers(workerPids);

  EXPECT_TRUE(supervisorCompleted) << "Supervisor should complete after worker interruption";
  EXPECT_FALSE(supervisorFailed) << supervisorError;
  AssertOutputTreeIsValid(testFile, ExpectedEntriesFromBins(kInterruptTestBins));

  std::remove(testFile.c_str());
}

TEST(TcpLazyJoinTest, SpawnWorkersWorkerInterruptionRedistributesTasks)
{
  const std::string runBin = ResolveCliBinary("ndmspc-run");
  const std::string workerBin = ResolveCliBinary("ndmspc-worker");
  const std::string macroPath = ResolveNTcpTestMacroPath();
  ASSERT_FALSE(runBin.empty()) << "Could not locate ndmspc-run binary";
  ASSERT_FALSE(workerBin.empty()) << "Could not locate ndmspc-worker binary";
  ASSERT_FALSE(macroPath.empty()) << "Could not locate NTcpTest.C";

  const int port = ReserveFreeTcpPort();
  ASSERT_GT(port, 0) << "Failed to reserve a free TCP port";
  const std::string testFile = "test_NTcpTest_spawn_worker_interrupt.root";
  std::remove(testFile.c_str());

  ScopedEnv scopedLogConsole("NDMSPC_LOG_CONSOLE");
  ScopedEnv scopedWorkerTimeout("NDMSPC_WORKER_TIMEOUT");
  ScopedEnv scopedStallTimeout("NDMSPC_IPC_STALL_TIMEOUT");
  ScopedEnv scopedTcpWorkerTimeout("NDMSPC_TCP_WORKER_TIMEOUT");

  const std::string macroParams = std::string("\"") + testFile + "\",\"" + kInterruptTestBins + "\",10";

  const pid_t supervisorPid = ::fork();
  ASSERT_GE(supervisorPid, 0) << "Failed to fork ndmspc-run supervisor";

  if (supervisorPid == 0) {
    gSystem->Setenv("NDMSPC_LOG_CONSOLE", "0");
    gSystem->Setenv("NDMSPC_WORKER_TIMEOUT", "5");
    gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT", "10");
    gSystem->Setenv("NDMSPC_TCP_WORKER_TIMEOUT", "5");

    ::execl(runBin.c_str(), runBin.c_str(),
            macroPath.c_str(),
            "--mode", "tcp",
            "--tcp-port", std::to_string(port).c_str(),
            "--spawn-workers", "2",
            "--worker-bin", workerBin.c_str(),
            "--macro-params", macroParams.c_str(),
            nullptr);
    _exit(127);
  }

  std::this_thread::sleep_for(std::chrono::seconds(3));

  const auto childPids = GetChildPids(supervisorPid);
  ASSERT_GE(childPids.size(), 2u) << "Expected ndmspc-run to spawn at least 2 workers";

  // Let the supervisor finish the initial READY/INIT handshake and start
  // dispatching real work before interrupting one spawned worker.
  std::this_thread::sleep_for(std::chrono::seconds(3));

  ::kill(childPids.front(), SIGINT);
  ::waitpid(childPids.front(), nullptr, 0);

  int status = 0;
  ASSERT_EQ(::waitpid(supervisorPid, &status, 0), supervisorPid) << "Failed waiting for ndmspc-run supervisor";
  ASSERT_TRUE(WIFEXITED(status)) << "ndmspc-run terminated abnormally";
  ASSERT_EQ(WEXITSTATUS(status), 0) << "ndmspc-run exited with non-zero status";

  CleanupWorkers(childPids);
  AssertOutputTreeIsValid(testFile, ExpectedEntriesFromBins(kInterruptTestBins));
  std::remove(testFile.c_str());
}

TEST(TcpLazyJoinTest, SingleThreadProcessesMacro)
{
  const std::string testFile = "test_NTcpTest_thread1.root";
  std::remove(testFile.c_str());

  ScopedEnv scopedMode("NDMSPC_EXECUTION_MODE");
  ScopedEnv scopedNProc("NDMSPC_MAX_PROCESSES");
  ScopedEnv scopedThreads("ROOT_MAX_THREADS");
  ScopedEnv scopedPort("NDMSPC_TCP_PORT");

  gSystem->Setenv("NDMSPC_EXECUTION_MODE", "thread");
  gSystem->Setenv("ROOT_MAX_THREADS", "1");
  gSystem->Unsetenv("NDMSPC_MAX_PROCESSES");
  gSystem->Unsetenv("NDMSPC_TCP_PORT");

  std::string error;
  ASSERT_TRUE(ExecuteNTcpTestMacro(testFile, 0, &error)) << error;
  AssertOutputTreeIsValid(testFile);
  std::remove(testFile.c_str());
}

TEST(TcpLazyJoinTest, IpcProcessesMacro)
{
  const std::string testFile = "test_NTcpTest_ipc4.root";
  std::remove(testFile.c_str());

  ScopedEnv scopedMode("NDMSPC_EXECUTION_MODE");
  ScopedEnv scopedNProc("NDMSPC_MAX_PROCESSES");
  ScopedEnv scopedThreads("ROOT_MAX_THREADS");
  ScopedEnv scopedPort("NDMSPC_TCP_PORT");
  ScopedEnv scopedWorkerTimeout("NDMSPC_WORKER_TIMEOUT");
  ScopedEnv scopedStallTimeout("NDMSPC_IPC_STALL_TIMEOUT");

  gSystem->Setenv("NDMSPC_EXECUTION_MODE", "ipc");
  gSystem->Setenv("ROOT_MAX_THREADS", "4");
  gSystem->Setenv("NDMSPC_MAX_PROCESSES", "4");
  gSystem->Setenv("NDMSPC_WORKER_TIMEOUT", "10");
  gSystem->Setenv("NDMSPC_IPC_STALL_TIMEOUT", "30");
  gSystem->Unsetenv("NDMSPC_TCP_PORT");

  std::string error;
  ASSERT_TRUE(ExecuteNTcpTestMacro(testFile, 0, &error)) << error;
  AssertOutputTreeIsValid(testFile);
  std::remove(testFile.c_str());
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char ** argv)
{
  if (argc > 0) gExecutablePath = argv[0];

  if (RunAsTcpWorkerIfRequested(argc, argv)) return 0;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
