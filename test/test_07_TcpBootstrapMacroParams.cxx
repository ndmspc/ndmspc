#include <gtest/gtest.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

#include <TSystem.h>

#include "NDimensionalIpcRunner.h"

#include <zmq.h>

namespace {

std::string gExecutablePath;

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

std::string FindWorkerBinary()
{
  const char * exeDirC = gSystem->DirName(gExecutablePath.c_str());
  if (!exeDirC || exeDirC[0] == '\0') return "";

  const std::string exeDir(exeDirC);
  const std::vector<std::string> candidates = {
    exeDir + "/../core/cli/ndmspc-worker",
    exeDir + "/../bin/ndmspc-worker",
    "ndmspc-worker",
  };

  for (const auto & candidate : candidates) {
    if (::access(candidate.c_str(), X_OK) == 0) return candidate;
  }

  return "";
}

pid_t SpawnWorker(const std::string & workerBin, const std::string & endpoint,
                  const std::string & macroParams = "")
{
  const pid_t pid = ::fork();
  if (pid != 0) return pid;

  if (!macroParams.empty()) {
    ::execl(workerBin.c_str(), workerBin.c_str(), "--endpoint", endpoint.c_str(),
            "--macro-params", macroParams.c_str(), nullptr);
  } else {
    ::execl(workerBin.c_str(), workerBin.c_str(), "--endpoint", endpoint.c_str(), nullptr);
  }
  _exit(127);
}

bool WaitForExitWithTimeout(pid_t pid, int timeoutSec, int & status)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t rc = ::waitpid(pid, &status, WNOHANG);
    if (rc == pid) return true;
    if (rc < 0) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  ::kill(pid, SIGTERM);
  ::waitpid(pid, &status, 0);
  return false;
}

} // namespace

TEST(TcpBootstrapMacroParamsTest, WorkerReceivesAndExecutesBootstrapMacroParams)
{
  const std::string workerBin = FindWorkerBinary();
  ASSERT_FALSE(workerBin.empty()) << "Could not locate ndmspc-worker binary";

  const int port = ReserveFreeTcpPort();
  ASSERT_GT(port, 0) << "Failed to reserve free TCP port";
  const std::string endpoint = "tcp://127.0.0.1:" + std::to_string(port);

  const std::string testDir = ".ndmspc-test-bootstrap";
  gSystem->mkdir(testDir.c_str(), true);

  const std::string macroPath = testDir + "/bootstrap_macro.C";
  const std::string outPath   = testDir + "/bootstrap_macro.out";
  std::remove(outPath.c_str());

  {
    std::ofstream macroFile(macroPath);
    ASSERT_TRUE(macroFile.good()) << "Failed to create macro file at " << macroPath;
    macroFile << "#include <fstream>\n";
    macroFile << "void bootstrap_macro(int v, const char* tag) {\n";
    macroFile << "  std::ofstream out(\"" << outPath << "\");\n";
    macroFile << "  out << v << ':' << (tag ? tag : \"\");\n";
    macroFile << "}\n";
  }

  bool        supervisorSentConfig = false;
  std::string supervisorError;

  std::thread supervisor([&]() {
    void * ctx    = zmq_ctx_new();
    void * router = zmq_socket(ctx, ZMQ_ROUTER);
    if (!ctx || !router) {
      supervisorError = "Failed to initialize ZMQ context/router";
      if (router) zmq_close(router);
      if (ctx) zmq_ctx_term(ctx);
      return;
    }

    int timeoutMs = 500;
    zmq_setsockopt(router, ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));

    if (zmq_bind(router, endpoint.c_str()) != 0) {
      supervisorError = std::string("Failed to bind endpoint: ") + zmq_strerror(zmq_errno());
      zmq_close(router);
      zmq_ctx_term(ctx);
      return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
      std::vector<std::string> frames;
      if (!Ndmspc::NDimensionalIpcRunner::ReceiveFrames(router, frames)) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        supervisorError = "Failed while receiving frames";
        break;
      }

      if (frames.size() >= 2 && frames[1] == "BOOTSTRAP") {
        const std::string identity = frames[0];
        const bool sent = Ndmspc::NDimensionalIpcRunner::SendFrames(
            router, {identity, "CONFIG", "0", macroPath, "/tmp", "/tmp", "123,\"tcp_boot\""});
        if (!sent) {
          supervisorError = "Failed to send CONFIG frames";
        } else {
          supervisorSentConfig = true;
        }
        break;
      }
    }

    zmq_close(router);
    zmq_ctx_term(ctx);
  });

  const pid_t workerPid = SpawnWorker(workerBin, endpoint);
  ASSERT_GT(workerPid, 0) << "Failed to spawn ndmspc-worker";

  int  status     = 0;
  bool exited     = WaitForExitWithTimeout(workerPid, 20, status);
  supervisor.join();

  ASSERT_TRUE(supervisorError.empty()) << supervisorError;
  ASSERT_TRUE(supervisorSentConfig) << "Supervisor never sent CONFIG";
  ASSERT_TRUE(exited) << "Worker did not exit in time";
  ASSERT_TRUE(WIFEXITED(status)) << "Worker terminated abnormally";
  ASSERT_EQ(WEXITSTATUS(status), 0) << "Worker exited with non-zero status";

  std::ifstream outFile(outPath);
  ASSERT_TRUE(outFile.good()) << "Macro output file missing: " << outPath;

  std::string content;
  std::getline(outFile, content);
  ASSERT_EQ(content, "123:tcp_boot") << "Macro did not receive bootstrap macro params correctly";

  std::remove(outPath.c_str());
  std::remove(macroPath.c_str());
}

TEST(TcpBootstrapMacroParamsTest, WorkerCliMacroParamsOverrideBootstrapConfig)
{
  const std::string workerBin = FindWorkerBinary();
  ASSERT_FALSE(workerBin.empty()) << "Could not locate ndmspc-worker binary";

  const int port = ReserveFreeTcpPort();
  ASSERT_GT(port, 0) << "Failed to reserve free TCP port";
  const std::string endpoint = "tcp://127.0.0.1:" + std::to_string(port);

  const std::string testDir = ".ndmspc-test-bootstrap";
  gSystem->mkdir(testDir.c_str(), true);

  const std::string macroPath = testDir + "/bootstrap_override_macro.C";
  const std::string outPath   = testDir + "/bootstrap_override_macro.out";
  std::remove(outPath.c_str());

  {
    std::ofstream macroFile(macroPath);
    ASSERT_TRUE(macroFile.good()) << "Failed to create macro file at " << macroPath;
    macroFile << "#include <fstream>\n";
    macroFile << "void bootstrap_override_macro(int v, const char* tag) {\n";
    macroFile << "  std::ofstream out(\"" << outPath << "\");\n";
    macroFile << "  out << v << ':' << (tag ? tag : \"\");\n";
    macroFile << "}\n";
  }

  bool        supervisorSentConfig = false;
  std::string supervisorError;

  std::thread supervisor([&]() {
    void * ctx    = zmq_ctx_new();
    void * router = zmq_socket(ctx, ZMQ_ROUTER);
    if (!ctx || !router) {
      supervisorError = "Failed to initialize ZMQ context/router";
      if (router) zmq_close(router);
      if (ctx) zmq_ctx_term(ctx);
      return;
    }

    int timeoutMs = 500;
    zmq_setsockopt(router, ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));

    if (zmq_bind(router, endpoint.c_str()) != 0) {
      supervisorError = std::string("Failed to bind endpoint: ") + zmq_strerror(zmq_errno());
      zmq_close(router);
      zmq_ctx_term(ctx);
      return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
      std::vector<std::string> frames;
      if (!Ndmspc::NDimensionalIpcRunner::ReceiveFrames(router, frames)) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        supervisorError = "Failed while receiving frames";
        break;
      }

      if (frames.size() >= 2 && frames[1] == "BOOTSTRAP") {
        const std::string identity = frames[0];
        const bool sent = Ndmspc::NDimensionalIpcRunner::SendFrames(
            router, {identity, "CONFIG", "0", macroPath, "/tmp", "/tmp", "111,\"bootstrap\""});
        if (!sent) {
          supervisorError = "Failed to send CONFIG frames";
        } else {
          supervisorSentConfig = true;
        }
        break;
      }
    }

    zmq_close(router);
    zmq_ctx_term(ctx);
  });

  const pid_t workerPid = SpawnWorker(workerBin, endpoint, "999,\"cli_override\"");
  ASSERT_GT(workerPid, 0) << "Failed to spawn ndmspc-worker";

  int  status = 0;
  bool exited = WaitForExitWithTimeout(workerPid, 20, status);
  supervisor.join();

  ASSERT_TRUE(supervisorError.empty()) << supervisorError;
  ASSERT_TRUE(supervisorSentConfig) << "Supervisor never sent CONFIG";
  ASSERT_TRUE(exited) << "Worker did not exit in time";
  ASSERT_TRUE(WIFEXITED(status)) << "Worker terminated abnormally";
  ASSERT_EQ(WEXITSTATUS(status), 0) << "Worker exited with non-zero status";

  std::ifstream outFile(outPath);
  ASSERT_TRUE(outFile.good()) << "Macro output file missing: " << outPath;

  std::string content;
  std::getline(outFile, content);
  ASSERT_EQ(content, "999:cli_override")
      << "CLI macro params should override bootstrap CONFIG macro params";

  std::remove(outPath.c_str());
  std::remove(macroPath.c_str());
}

int main(int argc, char ** argv)
{
  if (argc > 0) gExecutablePath = argv[0];

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
