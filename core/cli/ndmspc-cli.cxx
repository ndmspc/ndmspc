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

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <TApplication.h>
#include <TError.h>
#include <TROOT.h>
#include <TSystem.h>
#include <httplib.h>

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

// ===========================================================================
//  record — replay the tool tours declared in tools/list
// ===========================================================================

struct RecordTour {
  std::string tool;
  std::string action;
  std::string id;
  std::string name;
  std::string description;
  int         index = 0;
  json        spec;
};

struct RecordOptions {
  std::string url         = "http://localhost:8080";
  std::string tours;
  std::string out         = "./ndmspc-recordings";
  std::string size        = "1920x1080";
  std::string session     = "ndmspc-record";
  std::string prompt;
  std::string review;
  std::string reviewOut;
  std::string writeStages;
  bool        list        = false;
  bool        jsonOut     = false;
  bool        dryRun      = false;
  bool        noReview    = false;
};

/// Splits a URL into the `scheme://host[:port]` an httplib client is built from and the path it posts
/// to, keeping any query the path carries.
bool SplitUrl(const std::string & url, std::string & origin, std::string & path, std::string & error)
{
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) {
    error = "URL has no scheme: " + url;
    return false;
  }
  const size_t pathStart = url.find('/', schemeEnd + 3);
  if (pathStart == std::string::npos) {
    origin = url;
    path   = "/";
    return true;
  }
  origin = url.substr(0, pathStart);
  path   = url.substr(pathStart);
  return true;
}

bool HttpPostJson(const std::string & url, const std::string & body, std::string & response, std::string & error)
{
  std::string origin;
  std::string path;
  if (!SplitUrl(url, origin, path, error)) return false;

  // httplib rather than curl: the server and the OIDC clients already speak it, so the CLI needs no
  // extra library. A `https://` origin picks the TLS client by itself when httplib is built with
  // OpenSSL (CPPHTTPLIB_OPENSSL_SUPPORT, set for these binaries).
  response.clear();
  try {
    httplib::Client client(origin);
    client.set_connection_timeout(20);
    client.set_read_timeout(20);
    client.set_follow_location(true);

    httplib::Headers headers = {{"Content-Type", "application/json"}};
    auto result = client.Post(path, headers, body, "application/json");
    if (!result) {
      error = httplib::to_string(result.error());
      return false;
    }
    response = result->body;
    if (result->status < 200 || result->status >= 300) {
      error = "HTTP " + std::to_string(result->status);
      return false;
    }
    return true;
  }
  catch (const std::exception & e) {
    // A scheme this build cannot speak (an `https://` URL when TLS was not compiled in) throws here.
    error = e.what();
    return false;
  }
}

/// The MCP endpoint behind a UI URL, keeping any room query the UI URL carries.
std::string ApiUrl(const std::string & uiUrl)
{
  const size_t query = uiUrl.find('?');
  std::string  base  = query == std::string::npos ? uiUrl : uiUrl.substr(0, query);
  const std::string tail = query == std::string::npos ? "" : uiUrl.substr(query);
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base + "/api/mcp" + tail;
}

std::string Trim(const std::string & value)
{
  const size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string Slug(const std::string & value)
{
  std::string out;
  for (char c : value) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
    else if (out.empty() || out.back() != '-') out += '-';
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "tour" : out;
}

std::vector<std::string> SplitCsv(const std::string & csv)
{
  std::vector<std::string> values;
  std::string              current;
  for (char c : csv) {
    if (c == ',') {
      values.push_back(Trim(current));
      current.clear();
    }
    else {
      current += c;
    }
  }
  values.push_back(Trim(current));
  return values;
}

bool LoadTours(const std::string & url, std::vector<RecordTour> & tours, std::string & error)
{
  std::string response;
  const std::string body =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
  if (!HttpPostJson(ApiUrl(url), body, response, error)) {
    error = "tools/list at " + ApiUrl(url) + ": " + error;
    return false;
  }

  json doc;
  try {
    doc = json::parse(response);
  }
  catch (const std::exception & e) {
    error = std::string("tools/list returned invalid JSON: ") + e.what();
    return false;
  }
  if (doc.contains("error") && !doc["error"].is_null()) {
    error = "tools/list: " + doc["error"].dump();
    return false;
  }

  const json * tools = nullptr;
  if (doc.contains("result") && doc["result"].is_object() && doc["result"].contains("tools")) tools = &doc["result"]["tools"];
  else if (doc.contains("payload") && doc["payload"].is_object() && doc["payload"].contains("tools")) tools = &doc["payload"]["tools"];
  else if (doc.contains("tools")) tools = &doc["tools"];
  if (tools == nullptr || !tools->is_array()) {
    error = "tools/list returned no tools array";
    return false;
  }

  for (const auto & tool : *tools) {
    if (!tool.is_object()) continue;
    const std::string toolName = tool.value("name", "");
    const json        meta     = tool.value("_meta", json::object());
    const std::string action   = meta.value("ndmspc.io/action", toolName);
    const json        list     = meta.value("ndmspc.io/tutorials", json::array());
    if (!list.is_array()) continue;

    int index = 0;
    for (const auto & spec : list) {
      if (spec.is_object()) {
        RecordTour tour;
        tour.tool        = toolName;
        tour.action      = action;
        tour.index       = index;
        tour.id          = action + "#" + std::to_string(index);
        tour.name        = spec.value("name", "Tour " + std::to_string(index + 1));
        tour.description = spec.value("description", "");
        tour.spec        = spec;
        tours.push_back(tour);
      }
      index++;
    }
  }
  return true;
}

const RecordTour * FindTour(const std::vector<RecordTour> & tours, const std::string & key)
{
  for (const auto & tour : tours) {
    if (tour.id == key || tour.name == key) return &tour;
  }
  for (const auto & tour : tours) {
    if (Slug(tour.name) == Slug(key)) return &tour;
  }
  return nullptr;
}

std::string MakePrompt(const RecordTour & tour, const RecordOptions & options)
{
  std::vector<std::string> anchors;
  auto                     addAnchor = [&anchors](const std::string & anchor) {
    if (anchor.empty()) return;
    for (const auto & known : anchors)
      if (known == anchor) return;
    anchors.push_back(anchor);
  };

  const json steps = tour.spec.value("steps", json::array());
  for (const auto & step : steps) {
    if (!step.is_object()) continue;
    addAnchor(step.value("show", std::string()));
    addAnchor(step.value("advanceOn", std::string()));
  }

  std::string out;
  out += "# Record tour: " + tour.name + "\n\n";
  out += "You are recording one NDMSPC tool tour with `agent-browser`, at `" + options.size + "`, 30 fps.\n\n";
  out += "- Tour id: `" + tour.id + "`\n";
  out += "- Tool: `" + tour.tool + "` (action `" + tour.action + "`)\n";
  out += "- UI: " + options.url + "\n";
  out += "- Output: " + options.out + "/" + Slug(tour.name) + ".webm + .png\n\n";
  out += "## Rules\n\n";
  out += "1. The tour card must never appear: set `localStorage[\"ndmspc:tour:1:done\"]=\"1\"`";
  out += " (and the help-tip keys) before the app loads.\n";
  out += "2. Drive the page; do not start the tour. The actions below come from the tour metadata.\n";
  out += "3. One in-page eval per stage; keep each under 30 s (CDP limit).\n";
  out += "4. Use the synthetic cursor/ripple and type at ~45 ms per character.\n";
  out += "5. Wait for real state (`data-tour` anchors, pads, dialogs), not fixed sleeps.\n\n";
  out += "## Anchors\n\n";
  for (const auto & anchor : anchors) out += "- `" + anchor + "`\n";
  out += "\n## Tour JSON\n\n```json\n" + tour.spec.dump(2) + "\n```\n";
  out += "\nAfter recording, review the clip and re-run:\n\n```\n";
  out += "ndmspc-cli record --tours \"" + tour.name + "\" --out " + options.out + "\n```\n";
  return out;
}

bool CommandExists(const std::string & name)
{
  const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

std::string CommandOutput(const std::string & cmd)
{
  std::string out;
  FILE *      pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) return out;
  char buffer[256];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) out += buffer;
  pclose(pipe);
  return Trim(out);
}

bool VersionAtLeast(const std::string & version, int major, int minor)
{
  const size_t first = version.find_first_of("0123456789");
  if (first == std::string::npos) return false;
  int  parsedMajor = 0;
  int  parsedMinor = 0;
  char tail        = 0;
  if (std::sscanf(version.c_str() + first, "%d.%d%c", &parsedMajor, &parsedMinor, &tail) < 2) return false;
  return parsedMajor > major || (parsedMajor == major && parsedMinor >= minor);
}

/// Check the tools the recorder needs; ndmspc does not ship them and never installs them.
int RecordPreflight(std::string & message)
{
  if (!CommandExists("node")) {
    message =
        "Node.js 20+ is required to record, but 'node' was not found.\n"
        "Install Node, then the browser recorder:\n"
        "  npm install -g agent-browser\n"
        "ndmspc does not ship or install these tools.";
    return 2;
  }
  const std::string nodeVersion = CommandOutput("node --version 2>/dev/null");
  if (!VersionAtLeast(nodeVersion, 20, 0)) {
    message = "Node.js 20+ is required to record; found '" + nodeVersion + "'.";
    return 2;
  }
  if (!CommandExists("agent-browser")) {
    message =
        "'agent-browser' is required to record, but it was not found.\n"
        "Install it once with:\n"
        "  npm install -g agent-browser\n"
        "ndmspc does not ship or install it.";
    return 2;
  }
  const std::string abVersion = CommandOutput("agent-browser --version 2>/dev/null");
  if (!abVersion.empty() && !VersionAtLeast(abVersion, 0, 41)) {
    message = "agent-browser 0.41+ is required to record; found '" + abVersion + "'.\n"
              "Upgrade with: npm install -g agent-browser";
    return 2;
  }
  return 0;
}

std::string FindRecorder()
{
  if (const char * env = std::getenv("NDMSPC_RECORD_SCRIPT"); env != nullptr && *env != '\0') {
    if (std::filesystem::exists(env)) return env;
  }
  std::vector<std::string> candidates;
  if (const char * dir = gSystem->Getenv("NDMSPC_DIR"); dir != nullptr && *dir != '\0') {
    candidates.push_back(std::string(dir) + "/scripts/js/record/record-tour.mjs");
  }
  candidates.push_back("/usr/share/ndmspc/scripts/js/record/record-tour.mjs");

  std::error_code ec;
  const auto      exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    const auto bin = exe.parent_path();
    candidates.push_back((bin / "../share/ndmspc/scripts/js/record/record-tour.mjs").lexically_normal().string());
  }
  for (const auto & candidate : candidates) {
    if (std::filesystem::exists(candidate)) return candidate;
  }
  return "";
}

int RunProcess(const std::vector<std::string> & args)
{
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (const auto & arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    execvp(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) return 1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 1;
}

int PrintReview(const RecordOptions & options)
{
  const std::filesystem::path dir = options.reviewOut.empty() ? options.out : options.reviewOut;
  std::error_code             ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    NLogError("ndmspc-cli record: no recordings in '%s'", dir.c_str());
    return 4;
  }

  std::filesystem::path newest;
  std::filesystem::file_time_type newestTime{};
  if (options.review == "latest") {
    for (const auto & entry : std::filesystem::directory_iterator(dir, ec)) {
      const auto path = entry.path();
      if (path.extension() != ".md" || path.filename().string().find(".review.md") == std::string::npos) continue;
      const auto time = std::filesystem::last_write_time(path, ec);
      if (newest.empty() || (!ec && time > newestTime)) {
        newest     = path;
        newestTime = time;
      }
    }
  }
  else {
    const RecordTour * tour = nullptr;
    std::vector<RecordTour> tours;
    std::string             error;
    if (LoadTours(options.url, tours, error)) tour = FindTour(tours, options.review);
    const std::string wanted = tour != nullptr ? Slug(tour->name) : Slug(options.review);
    const auto        direct = dir / (wanted + ".review.md");
    if (std::filesystem::exists(direct)) newest = direct;
    else {
      for (const auto & entry : std::filesystem::directory_iterator(dir, ec)) {
        const auto path = entry.path();
        if (path.filename().string().find(wanted) != std::string::npos &&
            path.filename().string().find(".review.md") != std::string::npos) {
          newest = path;
          break;
        }
      }
    }
  }

  if (newest.empty()) {
    NLogError("ndmspc-cli record: no review prompt found for '%s' in '%s'", options.review.c_str(), dir.c_str());
    return 4;
  }
  std::ifstream input(newest);
  if (!input) {
    NLogError("ndmspc-cli record: cannot read '%s'", newest.c_str());
    return 4;
  }
  std::printf("%s", std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()).c_str());
  return 0;
}

int RunRecord(const RecordOptions & options)
{
  std::vector<RecordTour> tours;
  std::string             error;
  if (!LoadTours(options.url, tours, error)) {
    NLogError("ndmspc-cli record: %s", error.c_str());
    return 1;
  }

  if (options.list) {
    json listing = json::array();
    for (const auto & tour : tours) {
      listing.push_back(json{{"id", tour.id}, {"name", tour.name}, {"tool", tour.tool},
                             {"description", tour.description}, {"steps", tour.spec.value("steps", json::array()).size()}});
    }
    if (options.jsonOut) {
      std::printf("%s\n", listing.dump(2).c_str());
      return 0;
    }
    if (listing.empty()) {
      std::printf("No tool tours in %s.\n", ApiUrl(options.url).c_str());
      std::printf("Some tours appear only once a session is open (the NGNT workflow, for one).\n");
      return 0;
    }
    for (const auto & entry : listing) {
      std::printf("%-28s %s\n", entry["id"].get<std::string>().c_str(), entry["name"].get<std::string>().c_str());
    }
    return 0;
  }

  if (!options.prompt.empty()) {
    const RecordTour * tour = FindTour(tours, options.prompt);
    if (tour == nullptr) {
      NLogError("ndmspc-cli record: no tour '%s' (try --list)", options.prompt.c_str());
      return 3;
    }
    const std::string prompt = MakePrompt(*tour, options);
    if (options.out != "./ndmspc-recordings") {
      std::ofstream output(options.out);
      if (!output) {
        NLogError("ndmspc-cli record: cannot write '%s'", options.out.c_str());
        return 4;
      }
      output << prompt;
      std::printf("Wrote %s\n", options.out.c_str());
    }
    else {
      std::printf("%s", prompt.c_str());
    }
    return 0;
  }

  if (!options.review.empty()) return PrintReview(options);

  std::vector<const RecordTour *> selected;
  if (options.tours.empty()) {
    NLogError("ndmspc-cli record: choose tours with --tours (see --list), or --prompt/--review/--list");
    return 1;
  }
  for (const auto & key : SplitCsv(options.tours)) {
    if (key.empty()) continue;
    const RecordTour * tour = FindTour(tours, key);
    if (tour == nullptr) {
      NLogError("ndmspc-cli record: no tour '%s' (try --list)", key.c_str());
      return 3;
    }
    selected.push_back(tour);
  }
  if (selected.empty()) {
    NLogError("ndmspc-cli record: no tours selected");
    return 3;
  }

  int preflight = RecordPreflight(error);
  if (preflight != 0) {
    NLogError("ndmspc-cli record: %s", error.c_str());
    return preflight;
  }

  const std::string recorder = FindRecorder();
  if (recorder.empty()) {
    NLogError("ndmspc-cli record: recorder not found (set NDMSPC_RECORD_SCRIPT)");
    return 4;
  }
  std::filesystem::create_directories(options.out);

  for (const auto * tour : selected) {
    const auto tourFile = std::filesystem::temp_directory_path() /
                          ("ndmspc-record-" + std::to_string(getpid()) + "-" + std::to_string(tour->index) + ".json");
    {
      std::ofstream output(tourFile);
      if (!output) {
        NLogError("ndmspc-cli record: cannot write '%s'", tourFile.c_str());
        return 4;
      }
      output << tour->spec.dump(2);
    }

    std::vector<std::string> args{"node", recorder, "--url", options.url, "--tour-json", tourFile.string(),
                                  "--tour-id", tour->id, "--tour-name", tour->name, "--slug", Slug(tour->name),
                                  "--out", options.out, "--size", options.size, "--session", options.session};
    if (options.noReview) args.push_back("--no-review");
    if (options.dryRun) args.push_back("--dry-run");
    if (!options.writeStages.empty()) {
      args.push_back("--write-stages");
      args.push_back(options.writeStages);
    }

    if (options.dryRun) {
      NLogPrint("ndmspc-cli record: planning %s (%s)", tour->name.c_str(), tour->id.c_str());
    }
    else {
      NLogPrint("ndmspc-cli record: recording %s (%s)", tour->name.c_str(), tour->id.c_str());
    }
    std::fflush(nullptr);
    const int rc = RunProcess(args);
    std::error_code ec;
    std::filesystem::remove(tourFile, ec);
    if (rc != 0) {
      NLogError("ndmspc-cli record: recording '%s' failed (exit %d)", tour->name.c_str(), rc);
      return 4;
    }
  }
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

  // ---------------------------------------------------------------------------
  //  record — replay the tool tours declared in tools/list
  // ---------------------------------------------------------------------------
  std::string recordUrl         = "http://localhost:8080";
  std::string recordTours;
  std::string recordOut         = "./ndmspc-recordings";
  std::string recordSize        = "1920x1080";
  std::string recordSession     = "ndmspc-record";
  std::string recordPrompt;
  std::string recordReview;
  std::string recordReviewOut;
  std::string recordWriteStages;
  bool        recordList        = false;
  bool        recordJson        = false;
  bool        recordDryRun      = false;
  bool        recordNoReview    = false;

  auto * recordCmd = app.add_subcommand(
      "record", "Record the tool tours declared by the server (needs node and agent-browser)");
  recordCmd->add_flag("--list", recordList, "List the tool tours the server declares and exit");
  recordCmd->add_flag("--json", recordJson, "With --list: print JSON");
  recordCmd->add_option("--tours", recordTours, "Comma-separated tour names or ids to record");
  recordCmd->add_option("--url", recordUrl,
                        "UI URL of the server or room (default: http://localhost:8080)");
  recordCmd->add_option("--out", recordOut, "Directory for the videos (default: ./ndmspc-recordings)");
  recordCmd->add_option("--size", recordSize, "Viewport, WxH (default: 1920x1080)");
  recordCmd->add_option("--session", recordSession, "agent-browser session name");
  recordCmd->add_option("--prompt", recordPrompt, "Write the pre-record prompt for a tour and exit");
  recordCmd->add_option("--review", recordReview,
                        "Print the review prompt of a recorded tour, or 'latest'");
  recordCmd->add_option("--review-out", recordReviewOut, "Directory to look for review prompts in");
  recordCmd->add_option("--write-stages", recordWriteStages,
                        "Dump the generated stages to this directory (debugging only)");
  recordCmd->add_flag("--dry-run", recordDryRun, "Print the generated stages; record nothing");
  recordCmd->add_flag("--no-review", recordNoReview, "Do not write a post-record review prompt");

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

  if (*recordCmd) {
    // `record` talks to the user (missing tools, progress, errors): console only, and ROOT's
    // suppression from the default NDMSPC_LOG_CONSOLE=0 must be lifted again.
    gSystem->Setenv("NDMSPC_LOG_CONSOLE", "1");
    gSystem->Setenv("NDMSPC_LOG_RUN", "0");
    origConsoleEnv     = "1";
    origRunEnv         = "0";
    gErrorIgnoreLevel  = kWarning;
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
  else if (*recordCmd) {
    RecordOptions options;
    options.url         = recordUrl;
    options.tours       = recordTours;
    options.out         = recordOut;
    options.size        = recordSize;
    options.session     = recordSession;
    options.prompt      = recordPrompt;
    options.review      = recordReview;
    options.reviewOut   = recordReviewOut;
    options.writeStages = recordWriteStages;
    options.list        = recordList;
    options.jsonOut     = recordJson;
    options.dryRun      = recordDryRun;
    options.noReview    = recordNoReview;
    rc                  = RunRecord(options);
  }

  return Finish(rc);
}
