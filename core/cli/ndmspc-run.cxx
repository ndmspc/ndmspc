#include <CLI11.hpp>
#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <unistd.h>
#include "TROOT.h"
#include "TApplication.h"
#include "TSystem.h"
#include "NLogger.h"
#include "NUtils.h"
#include "ndmspc.h"

namespace {

pid_t spawn_worker_process(const std::string & workerBin, const std::string & endpoint, bool verbose)
{
  pid_t pid = fork();
  if (pid < 0) return -1;

  if (pid == 0) {
    if (verbose) {
      execlp(workerBin.c_str(), workerBin.c_str(), "--endpoint", endpoint.c_str(), "--verbose", nullptr);
    } else {
      execlp(workerBin.c_str(), workerBin.c_str(), "--endpoint", endpoint.c_str(), nullptr);
    }
    _exit(127);
  }

  return pid;
}

void stop_worker_processes(std::vector<pid_t> & workerPids)
{
  if (workerPids.empty()) return;

  for (pid_t pid : workerPids) {
    if (pid > 0) kill(pid, SIGTERM);
  }

  const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (pid_t pid : workerPids) {
    if (pid <= 0) continue;

    int status = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
      if (std::chrono::steady_clock::now() >= waitDeadline) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
  }

  workerPids.clear();
}

} // namespace

static std::string app_description()
{
  size_t size = 128;
  auto   buf  = std::make_unique<char[]>(size);
  size = std::snprintf(buf.get(), size, "%s v%s-%s (run)", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

int main(int argc, char ** argv)
{
  TApplication rootApp("ndmspc-run", 0, nullptr);
  gROOT->SetBatch(kTRUE);

  CLI::App app{app_description()};

  std::string macroList;
  std::string mode;          // ipc | tcp | thread (maps to NDMSPC_EXECUTION_MODE)
  size_t      nProcesses = 0; // 0 = not set
  std::string tcpPort;
  std::string workerBin;
  std::string workerEndpoint;
  std::string tmpDir;
  std::string tmpResultsDir;
  size_t      spawnWorkers = 0;
  bool        verbose = false;

  app.add_option("macro", macroList,
                 "Comma-separated list of macro file(s) or URLs to execute")
     ->required();
  app.add_option("--mode", mode,
                 "Execution mode: ipc/process (forked local processes), tcp (remote workers), thread")
     ->check(CLI::IsMember({"ipc", "process", "tcp", "thread"}));
  app.add_option("-n,--processes", nProcesses,
                 "Number of worker processes (NDMSPC_MAX_PROCESSES)");
  app.add_option("--tcp-port", tcpPort,
                 "TCP port to bind for remote workers (NDMSPC_TCP_PORT, default: 5555)");
  app.add_option("--spawn-workers", spawnWorkers,
                 "In TCP mode, spawn N local ndmspc-worker processes");
  app.add_option("--worker-bin", workerBin,
                 "Worker executable to spawn (default: ndmspc-worker)");
  app.add_option("--worker-endpoint", workerEndpoint,
                 "Worker endpoint for spawned workers (default: tcp://localhost:<tcp-port>)");
  app.add_option("--tmp-dir", tmpDir,
                 "Local scratch directory for temporary files (NDMSPC_TMP_DIR)");
  app.add_option("--results-dir", tmpResultsDir,
                 "Shared results directory where workers deposit output (NDMSPC_TMP_RESULTS_DIR)");
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

  // Set env vars from CLI args (only if not already set in the environment)
  auto setenvIfEmpty = [](const char * name, const std::string & value) {
    if (!value.empty() && !gSystem->Getenv(name))
      gSystem->Setenv(name, value.c_str());
  };

  if (mode == "process") mode = "ipc";
  if (!mode.empty() && !gSystem->Getenv("NDMSPC_EXECUTION_MODE"))
    gSystem->Setenv("NDMSPC_EXECUTION_MODE", mode.c_str());

  // When auto-spawning TCP workers, default NDMSPC_MAX_PROCESSES to that count
  // if the caller didn't set --processes / NDMSPC_MAX_PROCESSES explicitly.
  if (spawnWorkers > 0 && nProcesses == 0 && !gSystem->Getenv("NDMSPC_MAX_PROCESSES")) {
    nProcesses = spawnWorkers;
  }

  if (nProcesses > 0 && !gSystem->Getenv("NDMSPC_MAX_PROCESSES"))
    gSystem->Setenv("NDMSPC_MAX_PROCESSES", std::to_string(nProcesses).c_str());
  setenvIfEmpty("NDMSPC_TCP_PORT", tcpPort);
  setenvIfEmpty("NDMSPC_TMP_DIR", tmpDir);
  setenvIfEmpty("NDMSPC_TMP_RESULTS_DIR", tmpResultsDir);

  if (workerBin.empty()) workerBin = "ndmspc-worker";

  std::vector<pid_t> spawnedWorkers;
  auto cleanupSpawnedWorkers = [&]() { stop_worker_processes(spawnedWorkers); };

  if (spawnWorkers > 0) {
    const std::string effectiveMode = gSystem->Getenv("NDMSPC_EXECUTION_MODE") ? gSystem->Getenv("NDMSPC_EXECUTION_MODE") : "";
    if (effectiveMode != "tcp") {
      NLogError("ndmspc-run: --spawn-workers is supported only with --mode tcp (effective mode: '%s')",
                effectiveMode.c_str());
      return 1;
    }

    if (workerEndpoint.empty()) {
      const char * effectivePort = gSystem->Getenv("NDMSPC_TCP_PORT");
      workerEndpoint             = std::string("tcp://localhost:") + (effectivePort ? effectivePort : "5555");
    }

    NLogInfo("ndmspc-run: spawning %zu worker(s) using '%s' at %s", spawnWorkers, workerBin.c_str(),
             workerEndpoint.c_str());
    spawnedWorkers.reserve(spawnWorkers);
    for (size_t i = 0; i < spawnWorkers; ++i) {
      const pid_t pid = spawn_worker_process(workerBin, workerEndpoint, verbose);
      if (pid < 0) {
        NLogError("ndmspc-run: failed to spawn worker %zu", i);
        cleanupSpawnedWorkers();
        return 1;
      }
      spawnedWorkers.push_back(pid);
    }
  }

  // Expose the macro list so NGnTree::Process can forward it to TCP workers
  // automatically without requiring an explicit tree.SetWorkerMacro() call.
  gSystem->Setenv("NDMSPC_MACRO", macroList.c_str());

  std::vector<std::string> macros = Ndmspc::NUtils::Tokenize(macroList, ',');
  for (const auto & macro : macros) {
    NLogInfo("ndmspc-run: executing macro '%s'", macro.c_str());
    TMacro * m = Ndmspc::NUtils::OpenMacro(macro);
    if (!m) {
      NLogError("ndmspc-run: failed to open macro '%s', exiting", macro.c_str());
      cleanupSpawnedWorkers();
      return 1;
    }
    m->Exec();
    delete m;
  }

  cleanupSpawnedWorkers();
  NLogInfo("ndmspc-run: done");

  // // Workaround: ROOT teardown can intermittently hang at process exit in IPC
  // // batch mode after all useful work has already completed.
  // const std::string effectiveMode = gSystem->Getenv("NDMSPC_EXECUTION_MODE") ? gSystem->Getenv("NDMSPC_EXECUTION_MODE") : "";
  // if (effectiveMode == "ipc") {
  //   std::fflush(nullptr);
  //   _exit(0);
  // }

  return 0;
}
