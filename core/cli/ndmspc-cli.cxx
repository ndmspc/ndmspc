///
/// ndmspc-cli — command-line front-end for the file -> NGnTree and NGnTree merge jobs.
///
/// The two jobs are separate subcommands (they have different inputs, lifetimes and tuning):
///
///   ndmspc-cli file  [options]   build an NGnTree from a list of ROOT files
///   ndmspc-cli merge [options]   merge the objects of an NGnTree file into resumable partials
///   ndmspc-cli binning [options] objects + binnings -> per-bin projections in an NGnTree
///
/// Execution and logging behave like ndmspc-run: --mode / -n / --tcp-port / --tmp-dir /
/// --results-dir are exported to the NDMSPC_* environment (unless already set), and the logging
/// defaults follow NDMSPC_LOG_RUN / NDMSPC_LOG_CONSOLE (-v prefers console output).
///
/// Usage:
///   ndmspc-cli [--run] file  [-c cfg.json] [-j inline-json] [-f files.txt] [--objects a,b] [-o out.root]
///   ndmspc-cli [--run] merge [-c cfg.json] [-j inline-json] [-i in.root] [--objects a,*] [-o base.root]
///                            [--delete-partials] [--force] [--no-combine]
///   ndmspc-cli [--run] binning [-c cfg.json] [-j inline-json] [-i in.root|dir] [--objects a,*] [-o out.root]
///
/// By default a job only prints what it would do; pass --run to execute it.
///

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include <unistd.h>

#include <CLI/CLI.hpp>
#include <TApplication.h>
#include <TROOT.h>
#include <TSystem.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/ndmspc.h"

namespace {

std::string AppDescription()
{
  size_t size = 128;
  auto   buf  = std::make_unique<char[]>(size);
  size        = std::snprintf(buf.get(), size, "%s v%s-%s (cli)", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

std::string AppVersion()
{
  size_t size = 128;
  auto   buf  = std::make_unique<char[]>(size);
  size        = std::snprintf(buf.get(), size, "%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);
  return std::string(buf.get(), size);
}

/// Merge the configuration file (when given) and the inline JSON over the defaults already in cfg.
bool ApplyConfig(json & cfg, const std::string & cfgFile, const std::string & inlineJson)
{
  if (!cfgFile.empty() && !Ndmspc::NUtils::LoadJsonFileRelative(cfg, cfgFile, "")) {
    NLogError("ndmspc-cli: cannot load configuration '%s'", cfgFile.c_str());
    return false;
  }

  if (!inlineJson.empty()) {
    try {
      cfg.merge_patch(json::parse(inlineJson));
    }
    catch (const std::exception & e) {
      NLogError("ndmspc-cli: invalid --json: %s", e.what());
      return false;
    }
  }
  return true;
}

/// Leave the way ndmspc-run does: skip ROOT/Cling teardown unless NDMSPC_SKIP_ROOT_TEARDOWN=0.
int Finish(int rc)
{
  const char * skipTeardownEnv = gSystem->Getenv("NDMSPC_SKIP_ROOT_TEARDOWN");
  const bool   skipTeardown    = (skipTeardownEnv == nullptr) || (std::string(skipTeardownEnv) != "0");
  if (skipTeardown) {
    std::fflush(nullptr);
    _exit(rc);
  }
  return rc;
}

/// Print the object list: what the configuration holds plus the --objects CSV.
void PrintObjects(const json & cfg, const std::string & extraCsv)
{
  std::vector<std::string> objects = cfg.value("objects", std::vector<std::string>{});
  if (!extraCsv.empty()) objects.push_back(extraCsv);

  NLogPrint("ndmspc-cli: objects     : %zu", objects.size());
  for (const auto & o : objects) NLogPrint("ndmspc-cli:   %s", o.c_str());
}

void PrintConfig(const json & cfg)
{
  NLogPrint("ndmspc-cli: configuration:");
  NLogPrint("%s", cfg.dump(2).c_str());
}

/// What `file` would do, without touching anything.
int PrintPlanFile(const json & cfg, const std::string & outFile, const std::string & filesTxt,
                  const std::string & objectsCsv)
{
  NLogPrint("ndmspc-cli: planning only - pass --run to execute (nothing is written)");
  NLogPrint("ndmspc-cli: job         : file (ROOT files -> NGnTree)");
  NLogPrint("ndmspc-cli: output      : %s", outFile.empty() ? "(from config)" : outFile.c_str());

  if (!filesTxt.empty()) {
    NLogPrint("ndmspc-cli: file list   : %s (one path per line)", filesTxt.c_str());
  }
  else {
    std::vector<std::string> files = cfg.value("files", std::vector<std::string>{});
    NLogPrint("ndmspc-cli: files       : %zu", files.size());
    for (const auto & f : files) NLogPrint("ndmspc-cli:   %s", f.c_str());
  }

  const bool split = cfg.value("split", false);
  NLogPrint("ndmspc-cli: split       : %s", split ? "yes (one file per input)" : "no");
  if (split) {
    const std::string base = cfg.value("splitBase", std::string());
    NLogPrint("ndmspc-cli: split base  : %s", base.empty() ? "(default: .ndmspc/merge)" : base.c_str());
  }

  NLogPrint("ndmspc-cli: resume      : %s",
            cfg.value("reset", false) ? "no (--reset: redo every input)"
                                      : (split ? "yes (skip finished inputs)" : "n/a (aggregate output)"));

  const json chunkAxes = cfg.value("chunkAxes", json::array());
  if (!chunkAxes.empty()) {
    NLogPrint("ndmspc-cli: chunk axes  : %s", chunkAxes.dump().c_str());
    NLogPrint("ndmspc-cli: chunk >=    : %lld bytes",
              (long long)cfg.value("chunkMinBytes", (long long)0));
  }

  PrintObjects(cfg, objectsCsv);
  PrintConfig(cfg);
  return 0;
}

/// What `merge` would do, without touching anything.
int PrintPlanMerge(const json & cfg, const std::string & input, const std::string & base,
                   const std::string & objectsCsv, bool deletePartials)
{
  NLogPrint("ndmspc-cli: planning only - pass --run to execute (nothing is written)");
  NLogPrint("ndmspc-cli: job          : merge (NGnTree -> per-object partials)");
  NLogPrint("ndmspc-cli: input        : %s", input.empty() ? "(from config)" : input.c_str());

  bool inputSplit = cfg.value("inputSplit", false);
  if (!inputSplit) {
    const std::string in = input.empty() ? cfg.value("inputFile", std::string()) : input;
    inputSplit = Ndmspc::NUtils::IsSplitBase(in);
  }
  NLogPrint("ndmspc-cli: input split  : %s", inputSplit ? "yes (split output directory)" : "no");
  NLogPrint("ndmspc-cli: output base  : %s", base.empty() ? "(from config)" : base.c_str());
  NLogPrint("ndmspc-cli: partials     : <base without .root>.<object>.root");
  PrintObjects(cfg, objectsCsv);

  const char * envMode = gSystem->Getenv("NDMSPC_EXECUTION_MODE");
  const char * envProc = gSystem->Getenv("NDMSPC_MAX_PROCESSES");
  const char * envPort = gSystem->Getenv("NDMSPC_TCP_PORT");

  NLogPrint("ndmspc-cli: mode         : %s", envMode ? envMode : "(unset)");
  NLogPrint("ndmspc-cli: processes    : %s", envProc ? envProc : "(unset)");
  NLogPrint("ndmspc-cli: tcp port     : %s", envPort ? envPort : "(unset)");
  NLogPrint("ndmspc-cli: force        : %s", cfg.value("force", false) ? "yes" : "no");
  NLogPrint("ndmspc-cli: combine      : %s", cfg.value("combine", true) ? "yes" : "no");
  NLogPrint("ndmspc-cli: delete partials: %s", deletePartials ? "yes" : "no");
  NLogPrint("ndmspc-cli: resume       : %s",
            deletePartials ? "no (reset)" : "yes (skip complete partials)");
  PrintConfig(cfg);
  return 0;
}

/// What `project` would do, without touching anything.
int PrintPlanBinning(const json & cfg, const std::string & input, const std::string & out,
                     const std::string & objectsCsv)
{
  NLogPrint("ndmspc-cli: planning only - pass --run to execute (nothing is written)");
  NLogPrint("ndmspc-cli: job          : binning (objects + binnings -> projections)");
  NLogPrint("ndmspc-cli: input        : %s",
            input.empty() ? cfg.value("inputFile", std::string("(from config)")).c_str() : input.c_str());

  const std::string in = input.empty() ? cfg.value("inputFile", std::string()) : input;
  NLogPrint("ndmspc-cli: input split  : %s",
            Ndmspc::NUtils::IsSplitBase(in) ? "yes (split output directory)" : "no");
  NLogPrint("ndmspc-cli: output       : %s",
            out.empty() ? cfg.value("outFile", std::string("(from config)")).c_str() : out.c_str());
  NLogPrint("ndmspc-cli: object dir   : %s",
            cfg.value("objectDirectory", std::string("(root of the file)")).c_str());

  const json axes = cfg.value("axes", json::array());
  NLogPrint("ndmspc-cli: axes         : %s", axes.empty() ? "(all axes, original names)" : axes.dump().c_str());

  const json binnings = cfg.value("binnings", json::object());
  NLogPrint("ndmspc-cli: binnings     : %zu", binnings.size());
  for (auto it = binnings.begin(); it != binnings.end(); ++it) {
    NLogPrint("ndmspc-cli:   %s : %s", it.key().c_str(), it.value().dump().c_str());
  }

  if (cfg.contains("projectionName") && cfg["projectionName"].is_string()) {
    NLogPrint("ndmspc-cli: projection   : %s", cfg["projectionName"].get<std::string>().c_str());
  }
  else {
    NLogPrint("ndmspc-cli: projection   : axis %d", cfg.value("projectionAxis", 0));
  }

  PrintObjects(cfg, objectsCsv);
  PrintConfig(cfg);
  return 0;
}

} // namespace

int main(int argc, char ** argv)
{
  gROOT->SetBatch(kTRUE);
  TApplication rootApp("ndmspc-cli", 0, nullptr);

  CLI::App app{AppDescription()};
  app.set_version_flag("--version", AppVersion(), "Print version information and exit");
  app.require_subcommand(1);

  // Run options, the same ones ndmspc-run exposes (exported to NDMSPC_* when not already set).
  std::string mode;
  std::string nProcesses;
  std::string tcpPort;
  std::string tmpDir;
  std::string resultsDir;
  bool        verbose = false;
  bool        run     = false;

  app.add_option("--mode", mode, "Execution mode: ipc/process, tcp, thread (NDMSPC_EXECUTION_MODE)")
      ->check(CLI::IsMember({"ipc", "process", "tcp", "thread"}));
  app.add_option("-n,--processes", nProcesses,
                 "Number of worker processes (NDMSPC_MAX_PROCESSES). Accepts an integer or '2:3'");
  app.add_option("--tcp-port", tcpPort, "TCP port for remote workers (NDMSPC_TCP_PORT, default: 5555)");
  app.add_option("--tmp-dir", tmpDir, "Local scratch directory (NDMSPC_TMP_DIR)");
  app.add_option("--results-dir", resultsDir, "Shared results directory (NDMSPC_TMP_RESULTS_DIR)");
  app.add_flag("-v,--verbose", verbose, "Enable console logging");
  app.add_flag("--run", run, "Execute the job (default: only print what would be done)");

  // ---------------------------------------------------------------------------
  //  file — build the NGnTree from ROOT files
  // ---------------------------------------------------------------------------
  std::string fileCfg, fileJson, fileOut, fileObjects, fileList, fileSplitBase;
  bool        fileNoSplit = false;
  bool        fileReset   = false;
  std::vector<std::string> fileChunkAxes;
  long long   fileChunkMinBytes = -1;

  auto * fileCmd = app.add_subcommand("file", "Build an NGnTree from a list of ROOT files (one bin per file)");
  fileCmd->fallthrough(); // accept the run options (--run/-v/--mode/...) after the subcommand too
  fileCmd->add_option("-c,--config", fileCfg, "JSON configuration file (merged over the built-in defaults)");
  fileCmd->add_option("-j,--json", fileJson, "Inline JSON configuration (merged over --config/defaults)");
  fileCmd->add_option("-o,--out", fileOut, "Output NGnTree file (overrides the config 'outFile')");
  fileCmd->add_option("--objects", fileObjects, "Comma-separated object list, appended to the config 'objects'");
  fileCmd->add_option("-f,--files", fileList, "Text file with one input file path per line (overrides 'files')");
  fileCmd->add_flag("--no-split", fileNoSplit, "Write one aggregate tree instead of one file per input");
  fileCmd->add_option("--split-base", fileSplitBase, "Directory for the split output (overrides 'splitBase')");
  fileCmd->add_option("--chunk-axis", fileChunkAxes, "Chunk THnSparse on AXIS:BINS (repeatable, e.g. 0:4)");
  fileCmd->add_option("--chunk-min-bytes", fileChunkMinBytes,
                      "Only chunk objects at least this big (default 64 MB, 0 = every sparse)");
  fileCmd->add_flag("--reset", fileReset,
                    "Start from scratch: redo every input (default: skip inputs already finished)");

  // ---------------------------------------------------------------------------
  //  merge — merge the objects of an NGnTree file
  // ---------------------------------------------------------------------------
  std::string mergeCfg, mergeJson, mergeIn, mergeOut, mergeObjects, mergeExclude;
  bool        mergeForce      = false;
  bool        mergeNoCombine  = false;
  bool        deletePartials  = false;
  bool        mergeReset      = false;
  bool        mergeInputSplit = false;
  bool        mergeCombineChunks = false;
  long long   mergeSerializeBytes = -1;

  auto * mergeCmd = app.add_subcommand("merge", "Merge the objects of an NGnTree file into per-object partials");
  mergeCmd->fallthrough(); // accept the run options (--run/-v/--mode/...) after the subcommand too
  mergeCmd->add_option("-c,--config", mergeCfg, "JSON configuration file (merged over the built-in defaults)");
  mergeCmd->add_option("-j,--json", mergeJson, "Inline JSON configuration (merged over --config/defaults)");
  mergeCmd->add_option("-i,--input", mergeIn, "Input NGnTree file (overrides the config 'inputFile')");
  mergeCmd->add_option("-o,--out", mergeOut, "Base name for the partials and the combined file (overrides 'outFile')");
  mergeCmd->add_option("--objects", mergeObjects, "Comma-separated object names/patterns, appended to 'objects'");
  mergeCmd->add_option("--exclude-objects", mergeExclude,
                       "Comma-separated object names/patterns to skip (appended to 'excludeObjects')");
  mergeCmd->add_flag("--force", mergeForce, "Redo complete partials");
  mergeCmd->add_flag("--no-combine", mergeNoCombine, "Do not write the combined file");
  mergeCmd->add_flag("--delete-partials", deletePartials, "Delete existing partials and the combined file first");
  mergeCmd->add_flag("--reset", mergeReset,
                     "Start from scratch: delete existing partials and the combined file (same as --delete-partials)");
  mergeCmd->add_flag("--input-split", mergeInputSplit, "Input is a split-output directory (manifest.json)");
  mergeCmd->add_option("--serialize-bytes", mergeSerializeBytes,
                       "Merge objects at least this big one at a time (0 = off, default 64 MB)");
  mergeCmd->add_flag("--combine-chunks", mergeCombineChunks,
                     "Add the per-chunk results back into one object (default: keep chunk files)");

  // ---------------------------------------------------------------------------
  //  binning — objects + binnings -> projections
  // ---------------------------------------------------------------------------
  std::string binningCfg, binningJson, binningIn, binningOut, binningObjects, binningExclude;

  auto * binningCmd =
      app.add_subcommand("binning", "Build an NGnTree from objects with several binnings and store per-bin projections");
  binningCmd->fallthrough(); // accept the run options (--run/-v/--mode/...) after the subcommand too
  binningCmd->add_option("-c,--config", binningCfg, "JSON configuration file (merged over the built-in defaults)");
  binningCmd->add_option("-j,--json", binningJson, "Inline JSON configuration (merged over --config/defaults)");
  binningCmd->add_option("-i,--input", binningIn, "Input ROOT file or split-output directory (overrides 'inputFile')");
  binningCmd->add_option("-o,--out", binningOut, "Output NGnTree file (overrides 'outFile')");
  binningCmd->add_option("--objects", binningObjects,
                         "Comma-separated object names/patterns, appended to 'objects'");
  binningCmd->add_option("--exclude-objects", binningExclude,
                         "Comma-separated object names/patterns to skip (appended to 'excludeObjects')");

  CLI11_PARSE(app, argc, argv);

  // Logging defaults, exactly as ndmspc-run sets them up.
  const char * origRunEnv     = gSystem->Getenv("NDMSPC_LOG_RUN");
  const char * origConsoleEnv = gSystem->Getenv("NDMSPC_LOG_CONSOLE");
  const bool   hadRun         = origRunEnv != nullptr;
  const bool   hadConsole     = origConsoleEnv != nullptr;

  if (!hadRun && !hadConsole) {
    // Neither set: run logging enabled, console disabled.
    gSystem->Setenv("NDMSPC_LOG_RUN", "1");
    gSystem->Setenv("NDMSPC_LOG_CONSOLE", "0");
    origRunEnv     = "1";
    origConsoleEnv = "0";
  }
  else {
    if (!hadConsole) {
      gSystem->Setenv("NDMSPC_LOG_CONSOLE", "0");
      origConsoleEnv = "0";
    }
    if (!hadRun) {
      if (hadConsole) {
        gSystem->Setenv("NDMSPC_LOG_RUN", "0");
        origRunEnv = "0";
      }
      else {
        gSystem->Setenv("NDMSPC_LOG_RUN", "1");
        origRunEnv = "1";
      }
    }
  }

  if (verbose) {
    gSystem->Setenv("NDMSPC_LOG_CONSOLE", "1");
    gSystem->Setenv("NDMSPC_LOG_RUN", "0");
    origConsoleEnv = "1";
    origRunEnv     = "0";
  }

  Ndmspc::NLogger::SetRunOutput(std::string(origRunEnv ? origRunEnv : "0") == "1");
  Ndmspc::NLogger::SetConsoleOutput(std::string(origConsoleEnv ? origConsoleEnv : "0") == "1");

  // Run options -> NDMSPC_* (only when the environment does not already define them).
  auto setenvIfEmpty = [](const char * name, const std::string & value) {
    if (!value.empty() && !gSystem->Getenv(name)) gSystem->Setenv(name, value.c_str());
  };

  if (mode == "process") mode = "ipc";
  setenvIfEmpty("NDMSPC_EXECUTION_MODE", mode);
  setenvIfEmpty("NDMSPC_MAX_PROCESSES", nProcesses);
  setenvIfEmpty("NDMSPC_TCP_PORT", tcpPort);
  setenvIfEmpty("NDMSPC_TMP_DIR", tmpDir);
  setenvIfEmpty("NDMSPC_TMP_RESULTS_DIR", resultsDir);

  int rc = 1;

  if (*fileCmd) {
    json cfg = Ndmspc::NFileToNGnTreeDefaults;
    if (!ApplyConfig(cfg, fileCfg, fileJson)) return Finish(1);
    if (fileNoSplit) cfg["split"] = false;
    if (!fileSplitBase.empty()) cfg["splitBase"] = fileSplitBase;
    if (!fileChunkAxes.empty()) {
      json chunkAxes = json::array();
      for (const auto & spec : fileChunkAxes) {
        const size_t pos = spec.find(':');
        if (pos == std::string::npos) {
          NLogError("ndmspc-cli: invalid --chunk-axis '%s' (expected AXIS:BINS)", spec.c_str());
          return Finish(1);
        }
        const std::string axis = spec.substr(0, pos);
        const std::string bins = spec.substr(pos + 1);
        json              entry;
        if (axis.find_first_not_of("0123456789") == std::string::npos) entry["axis"] = std::stoi(axis);
        else entry["axis"] = axis;
        entry["bins"] = std::stoi(bins);
        chunkAxes.push_back(entry);
      }
      cfg["chunkAxes"] = chunkAxes;
    }
    if (fileChunkMinBytes >= 0) cfg["chunkMinBytes"] = fileChunkMinBytes;
    if (fileReset) cfg["reset"] = true;
    if (!run) return Finish(PrintPlanFile(cfg, fileOut, fileList, fileObjects));

    rc = Ndmspc::NUtils::FileToNGnTree(cfg, fileOut, fileList, fileObjects, "") ? 0 : 1;
  }
  else if (*mergeCmd) {
    json cfg = Ndmspc::NGnTreeMergeDefaults;
    if (!ApplyConfig(cfg, mergeCfg, mergeJson)) return Finish(1);

    if (!mergeExclude.empty()) {
      std::vector<std::string> excludes = cfg.value("excludeObjects", std::vector<std::string>{});
      std::stringstream        ss(mergeExclude);
      std::string              token;
      while (std::getline(ss, token, ',')) {
        const size_t first = token.find_first_not_of(" \t");
        const size_t last  = token.find_last_not_of(" \t");
        if (first == std::string::npos) continue;
        excludes.push_back(token.substr(first, last - first + 1));
      }
      cfg["excludeObjects"] = excludes;
    }

    if (!mergeIn.empty()) cfg["inputFile"] = mergeIn;
    if (mergeForce) cfg["force"] = true;
    if (mergeNoCombine) cfg["combine"] = false;
    if (mergeInputSplit) cfg["inputSplit"] = true;
    if (mergeCombineChunks) cfg["combineChunks"] = true;
    if (mergeSerializeBytes >= 0) cfg["serializeBytes"] = mergeSerializeBytes;
    if (mergeReset) deletePartials = true;

    if (!run) return Finish(PrintPlanMerge(cfg, mergeIn, mergeOut, mergeObjects, deletePartials));

    rc = Ndmspc::NUtils::NGnTreeMerge(cfg, mergeOut, mergeObjects, deletePartials, "") ? 0 : 1;
  }
  else if (*binningCmd) {
    json cfg = Ndmspc::NGnTreeBinningDefaults;
    if (!ApplyConfig(cfg, binningCfg, binningJson)) return Finish(1);

    auto appendCsv = [&cfg](const std::string & key, const std::string & csv) {
      if (csv.empty()) return;
      std::vector<std::string> values = cfg.value(key, std::vector<std::string>{});
      std::stringstream        ss(csv);
      std::string              token;
      while (std::getline(ss, token, ',')) {
        const size_t first = token.find_first_not_of(" \t");
        const size_t last  = token.find_last_not_of(" \t");
        if (first == std::string::npos) continue;
        values.push_back(token.substr(first, last - first + 1));
      }
      cfg[key] = values;
    };

    appendCsv("objects", binningObjects);
    appendCsv("excludeObjects", binningExclude);

    if (!binningIn.empty()) cfg["inputFile"] = binningIn;
    if (!binningOut.empty()) cfg["outFile"] = binningOut;

    if (!run) return Finish(PrintPlanBinning(cfg, binningIn, binningOut, ""));

    rc = Ndmspc::NUtils::NGnTreeBinning(cfg, "") ? 0 : 1;
  }

  return Finish(rc);
}
