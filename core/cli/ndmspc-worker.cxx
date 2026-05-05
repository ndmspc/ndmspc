#include <CLI11.hpp>
#include <csignal>
#include <chrono>
#include <sstream>
#include <sys/wait.h>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include "TROOT.h"
#include "TApplication.h"
#include "TSystem.h"
#include "NLogger.h"
#include "NUtils.h"
#include "ndmspc.h"
#include "NDimensionalIpcRunner.h"
#include <zmq.h>

static std::string app_description()
{
  size_t size = 64;
  auto   buf  = std::make_unique<char[]>(size);
  size = std::snprintf(buf.get(), size, "%s v%s-%s (worker)", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

static std::string app_version()
{
  size_t size = 128;
  auto   buf  = std::make_unique<char[]>(size);
  size = std::snprintf(buf.get(), size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

namespace {

volatile sig_atomic_t gTerminateRequested = 0;

void handle_signal(int)
{
  gTerminateRequested = 1;
}

pid_t spawn_worker_process(const std::string & workerBin, const std::string & endpoint, const std::string & mode,
                           const std::string & macroList, const std::string & macroParams, bool verbose)
{
  pid_t pid = fork();
  if (pid < 0) return -1;

  if (pid == 0) {
    std::vector<std::string> args;
    args.emplace_back(workerBin);
    args.emplace_back("--endpoint");
    args.emplace_back(endpoint);
    if (!mode.empty()) {
      args.emplace_back("--mode");
      args.emplace_back(mode);
    }
    if (!macroList.empty()) {
      args.emplace_back("--macro");
      args.emplace_back(macroList);
    }
    if (!macroParams.empty()) {
      args.emplace_back("--macro-params");
      args.emplace_back(macroParams);
    }
    if (verbose) args.emplace_back("--verbose");

    std::vector<char *> cargs;
    cargs.reserve(args.size() + 1);
    for (auto & arg : args) {
      cargs.push_back(const_cast<char *>(arg.c_str()));
    }
    cargs.push_back(nullptr);

    execvp(workerBin.c_str(), cargs.data());
    _exit(127);
  }

  return pid;
}

void terminate_workers(const std::vector<pid_t> & workerPids, int signalToSend)
{
  for (pid_t pid : workerPids) {
    if (pid > 0) kill(pid, signalToSend);
  }
}

int wait_for_workers(std::vector<pid_t> & workerPids)
{
  size_t aliveCount = workerPids.size();
  int    exitCode   = 0;

  while (aliveCount > 0) {
    if (gTerminateRequested) {
      terminate_workers(workerPids, SIGTERM);
    }

    bool progressed = false;
    for (pid_t & pid : workerPids) {
      if (pid <= 0) continue;
      int   status = 0;
      pid_t rc     = waitpid(pid, &status, WNOHANG);
      if (rc == 0) continue;
      if (rc < 0) {
        pid = -1;
        --aliveCount;
        progressed = true;
        exitCode   = 1;
        continue;
      }

      pid = -1;
      --aliveCount;
      progressed = true;
      if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0) exitCode = 1;
      } else {
        exitCode = 1;
      }
    }

    if (!progressed) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  if (gTerminateRequested) {
    return 128 + SIGTERM;
  }
  return exitCode;
}

} // namespace

int main(int argc, char ** argv)
{
  signal(SIGTERM, handle_signal);
  signal(SIGINT, handle_signal);

  TApplication rootApp("ndmspc-worker", 0, nullptr);

  CLI::App app{app_description()};
  app.set_version_flag("--version", app_version(), "Print version information and exit");

  std::string endpoint;
  size_t      workerIndex = std::numeric_limits<size_t>::max(); // sentinel: not set
  std::string macroList;
  std::string macroParams;
  std::string mode; // ipc | process | tcp | thread (optional override)
  std::string workerBin;
  size_t      spawnWorkers = 0;
  bool        verbose = false;

  app.add_option("-e,--endpoint", endpoint, "Master endpoint (e.g. tcp://host:5555)")->required();
  app.add_option("-i,--index", workerIndex, "Worker index (default: auto-assigned by supervisor)");
  app.add_option("-m,--macro", macroList, "Comma-separated list of macro file(s) or URLs to execute");
  app.add_option("--macro-params", macroParams,
                 "Parameter list forwarded to TMacro::Exec(params), e.g. '42,\"sample\"'");
  app.add_option("--mode", mode,
                 "Execution mode override: ipc/process (local), tcp (remote), thread")
     ->check(CLI::IsMember({"ipc", "process", "tcp", "thread"}));
  app.add_option("--spawn-workers", spawnWorkers,
                 "Spawn N local ndmspc-worker processes (spawner mode)");
  app.add_option("--worker-bin", workerBin,
                 "Worker executable for spawner mode (default: current executable)");
  app.add_flag("-v,--verbose", verbose, "Enable verbose logging");

  CLI11_PARSE(app, argc, argv);

  // Default to file-only logging unless explicitly configured by environment.
  if (!gSystem->Getenv("NDMSPC_LOG_CONSOLE")) {
    gSystem->Setenv("NDMSPC_LOG_CONSOLE", "0");
    if (!verbose) {
      Ndmspc::NLogger::SetConsoleOutput(false);
    }
  }

  if (verbose) {
    Ndmspc::NLogger::SetConsoleOutput(true);
  }

  if (mode == "process") mode = "ipc";
  if (!mode.empty() && !gSystem->Getenv("NDMSPC_EXECUTION_MODE")) {
    gSystem->Setenv("NDMSPC_EXECUTION_MODE", mode.c_str());
  }
  if (!macroParams.empty() && !gSystem->Getenv("NDMSPC_MACRO_PARAMS")) {
    gSystem->Setenv("NDMSPC_MACRO_PARAMS", macroParams.c_str());
  }

  if (spawnWorkers > 0) {
    if (workerIndex != std::numeric_limits<size_t>::max()) {
      NLogError("ndmspc-worker: --index cannot be combined with --spawn-workers");
      return 1;
    }
    if (workerBin.empty()) {
      workerBin = (argc > 0 && argv[0] && argv[0][0] != '\0') ? argv[0] : "ndmspc-worker";
    }

    NLogInfo("ndmspc-worker: spawning %zu worker(s) using '%s' -> %s", spawnWorkers, workerBin.c_str(),
             endpoint.c_str());
    std::vector<pid_t> workerPids;
    workerPids.reserve(spawnWorkers);
    for (size_t i = 0; i < spawnWorkers; ++i) {
      const pid_t pid = spawn_worker_process(workerBin, endpoint, mode, macroList, macroParams, verbose);
      if (pid < 0) {
        NLogError("ndmspc-worker: failed to spawn worker %zu", i);
        terminate_workers(workerPids, SIGTERM);
        return 1;
      }
      workerPids.push_back(pid);
    }
    return wait_for_workers(workerPids);
  }

  // If macro or index not provided, bootstrap from supervisor
  const bool needsBootstrap = macroList.empty() || workerIndex == std::numeric_limits<size_t>::max();
  if (needsBootstrap) {
    NLogPrint("ndmspc-worker: bootstrapping config from supervisor at %s ...", endpoint.c_str());
    void * ctx    = zmq_ctx_new();
    void * dealer = zmq_socket(ctx, ZMQ_DEALER);
    // Use a unique bootstrap identity so supervisor can route the CONFIG reply
    std::ostringstream bootIdBuilder;
    const char * host = gSystem->HostName();
    bootIdBuilder << "boot_" << (host ? host : "unknown") << '_' << gSystem->GetPid() << '_'
                  << std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::string bootId = bootIdBuilder.str();
    zmq_setsockopt(dealer, ZMQ_IDENTITY, bootId.data(), bootId.size());
    int timeoutMs = 1000;
    zmq_setsockopt(dealer, ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeoutMs));
    zmq_connect(dealer, endpoint.c_str());
    Ndmspc::NDimensionalIpcRunner::SendFrames(dealer, {"BOOTSTRAP"});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    bool       configOk = false;
    bool       rejectedBySupervisor = false;
    while (!configOk) {
      std::vector<std::string> frames;
      if (!Ndmspc::NDimensionalIpcRunner::ReceiveFrames(dealer, frames)) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          if (std::chrono::steady_clock::now() > deadline) break;
          continue;
        }
        break;
      }
      // CONFIG frames: "CONFIG", workerIdx, macroList, NDMSPC_TMP_DIR,
      // NDMSPC_TMP_RESULTS_DIR, [optional] NDMSPC_MACRO_PARAMS
      if (frames.size() >= 5 && frames[0] == "CONFIG") {
        if (workerIndex == std::numeric_limits<size_t>::max())
          workerIndex = static_cast<size_t>(std::stoul(frames[1]));
        if (macroList.empty())
          macroList = frames[2];
        if (!frames[3].empty())
          gSystem->Setenv("NDMSPC_TMP_DIR", frames[3].c_str());
        if (!frames[4].empty())
          gSystem->Setenv("NDMSPC_TMP_RESULTS_DIR", frames[4].c_str());
        if (frames.size() >= 6 && !frames[5].empty() && !gSystem->Getenv("NDMSPC_MACRO_PARAMS"))
          gSystem->Setenv("NDMSPC_MACRO_PARAMS", frames[5].c_str());
        configOk = true;
      } else if (frames.size() >= 1 && frames[0] == "REJECT") {
        const std::string reason = (frames.size() >= 2) ? frames[1] : "unspecified";
        NLogWarning("ndmspc-worker: bootstrap rejected by supervisor (%s), exiting", reason.c_str());
        rejectedBySupervisor = true;
        break;
      }
    }
    zmq_close(dealer);
    zmq_ctx_term(ctx);

    if (rejectedBySupervisor) {
      return 0;
    }

    if (!configOk) {
      NLogError("ndmspc-worker: failed to receive CONFIG from supervisor at %s, exiting", endpoint.c_str());
      return 1;
    }
  }

  // Fallback: if NDMSPC_TMP_RESULTS_DIR is unset or empty, use NDMSPC_TMP_DIR
  if (!gSystem->Getenv("NDMSPC_TMP_RESULTS_DIR") || gSystem->Getenv("NDMSPC_TMP_RESULTS_DIR")[0] == '\0') {
    const char * tmpDirEnv = gSystem->Getenv("NDMSPC_TMP_DIR");
    if (tmpDirEnv && tmpDirEnv[0] != '\0')
      gSystem->Setenv("NDMSPC_TMP_RESULTS_DIR", tmpDirEnv);
  }

  if (macroList.empty()) {
    NLogError("ndmspc-worker: no macro specified (use --macro or let supervisor send via bootstrap)");
    return 1;
  }
  if (workerIndex == std::numeric_limits<size_t>::max()) workerIndex = 0;

  // Set env vars so NGnTree::Process enters worker mode
  gSystem->Setenv("NDMSPC_WORKER_ENDPOINT", endpoint.c_str());
  gSystem->Setenv("NDMSPC_WORKER_INDEX", std::to_string(workerIndex).c_str());

  NLogPrint("ndmspc-worker: starting — index=%zu endpoint=%s", workerIndex, endpoint.c_str());
  NLogPrint("  macro                  = %s", macroList.c_str());
  NLogPrint("  NDMSPC_TMP_DIR         = %s", gSystem->Getenv("NDMSPC_TMP_DIR") ? gSystem->Getenv("NDMSPC_TMP_DIR") : "(not set)");
  NLogPrint("  NDMSPC_TMP_RESULTS_DIR = %s", gSystem->Getenv("NDMSPC_TMP_RESULTS_DIR") ? gSystem->Getenv("NDMSPC_TMP_RESULTS_DIR") : "(not set)");
  NLogPrint("  NDMSPC_EXECUTION_MODE  = %s", gSystem->Getenv("NDMSPC_EXECUTION_MODE") ? gSystem->Getenv("NDMSPC_EXECUTION_MODE") : "(not set)");
  NLogPrint("  NDMSPC_MACRO_PARAMS    = %s", gSystem->Getenv("NDMSPC_MACRO_PARAMS") ? gSystem->Getenv("NDMSPC_MACRO_PARAMS") : "(not set)");

  const std::string effectiveMacroParams = gSystem->Getenv("NDMSPC_MACRO_PARAMS") ? gSystem->Getenv("NDMSPC_MACRO_PARAMS") : "";
  std::vector<std::string> macros = Ndmspc::NUtils::Tokenize(macroList, ',');
  for (const auto & macro : macros) {
    if (effectiveMacroParams.empty()) {
      NLogInfo("ndmspc-worker: executing macro '%s'", macro.c_str());
    } else {
      NLogInfo("ndmspc-worker: executing macro '%s' with params '%s'", macro.c_str(), effectiveMacroParams.c_str());
    }
    TMacro * m = Ndmspc::NUtils::OpenMacro(macro);
    if (!m) {
      NLogError("ndmspc-worker: failed to open macro '%s', exiting", macro.c_str());
      return 1;
    }
    m->Exec(effectiveMacroParams.empty() ? nullptr : effectiveMacroParams.c_str());
    delete m;
  }

  NLogInfo("ndmspc-worker: done");
  return 0;
}
