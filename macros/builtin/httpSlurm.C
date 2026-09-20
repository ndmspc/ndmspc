///
/// httpSlurm.C — HTTP/MCP interface to the cluster's Slurm over the REST API.
/// Registers: slurm/submit, slurm/jobs, slurm/job, slurm/nodes
/// URLs:      POST /api/slurm/submit, GET /api/slurm/jobs,
///            GET|POST|DELETE /api/slurm/job, GET /api/slurm/nodes
///
/// slurm/job takes the job id in the request body ({"id":"42"}): the URL query is
/// reserved for the room router's ?room=<id>.
///
/// Every action is a call to slurmrestd and its answers are passed through, so the
/// pod needs no Slurm client, no /etc/slurm and no cluster key — only the REST
/// endpoint and a JWT (the X-SLURM-USER-NAME / X-SLURM-USER-TOKEN headers). The
/// token is minted for the SlurmUser account (devops role roles/ndmspc), and that
/// is what makes the job run as that account on the compute nodes, whatever uid
/// the pod itself runs as.
///
/// The token is read from a file, not from an environment variable: it carries
/// SlurmUser rights on the cluster. It is re-read per request, so a rotated Secret
/// is picked up.
///
/// Job output stays on the compute node a job ran on, so a room does not see it; a
/// log push through a Slurm epilog was removed and will be redone later.
///
/// Usage: ndmspc-server -m "<dir>/httpNgntBase.C,<dir>/httpNgnt.C,<dir>/httpSlurm.C"
///
/// Overridables:
///   NDMSPC_SLURM_REST_URL         (default http://slurm-restapi.slurm:6820)
///   NDMSPC_SLURM_REST_USER        (default slurm — the account to submit as)
///   NDMSPC_SLURM_REST_TOKEN_FILE  (default /var/run/slurm/token)
///   NDMSPC_SLURM_REST_VERSION     (default: negotiate the data_parser version)
///   NDMSPC_SLURM_RUN              (default /usr/bin/ndmspc-run)
///   NDMSPC_SLURM_DEFAULT_PARTITION (default "all")

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <ndmspc/http/NHttpRequest.h>
#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NRouteContext.h>

namespace {

std::string Env(const char *name, const char *def)
{
  const char *v = std::getenv(name);
  return (v && *v) ? std::string(v) : std::string(def);
}

std::string Trim(const std::string &s)
{
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Anything that reaches the Slurm REST API comes from a request body, so it is
// checked instead of trusted.
bool Safe(const std::string &s, size_t max = 512)
{
  return !s.empty() && s.size() <= max && s.find_first_of(";|&$`><\n\\\"'()") == std::string::npos;
}

std::string RestUrl()
{
  std::string url = Env("NDMSPC_SLURM_REST_URL", "http://slurm-restapi.slurm:6820");
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

std::string TokenFile() { return Env("NDMSPC_SLURM_REST_TOKEN_FILE", "/var/run/slurm/token"); }

std::string Token()
{
  std::ifstream in(TokenFile());
  if (!in) return "";
  std::string token;
  std::getline(in, token);
  return Trim(token);
}

std::map<std::string, std::string> Headers()
{
  return {{"Content-Type", "application/json"},
          {"X-SLURM-USER-NAME", Env("NDMSPC_SLURM_REST_USER", "slurm")},
          {"X-SLURM-USER-TOKEN", Token()}};
}

// slurmrestd serves one URL per data_parser version and has no "latest" alias: a
// wrong version is a plain 404. The room image is shared across clusters that do
// not run the same Slurm, so the version is negotiated once per process (newest
// first, against /ping) and then remembered. NDMSPC_SLURM_REST_VERSION pins it.
const char *kApiVersions[] = {"v0.0.46", "v0.0.45", "v0.0.44", "v0.0.43",
                              "v0.0.42", "v0.0.41", "v0.0.40"};
std::string gApiVersion;

std::string NegotiateApiVersion(Ndmspc::NHttpRequest &http, const std::map<std::string, std::string> &headers,
                                std::string &error)
{
  const std::string pinned = Env("NDMSPC_SLURM_REST_VERSION", "");
  if (!pinned.empty()) return pinned;

  const std::string base = RestUrl();
  int               last = 0;
  for (const char *version : kApiVersions) {
    try {
      const auto r = http.request("GET", base + "/slurm/" + version + "/ping", "", headers);
      if (r.status >= 200 && r.status < 300) return version;
      last = r.status;
    } catch (const std::exception &e) {
      error = "slurmrestd unreachable at " + base + " (" + e.what() + ")";
      return "";
    }
  }
  error = "no supported Slurm REST API version at " + base + " (tried v0.0.46 ... v0.0.40, last HTTP " +
          std::to_string(last) + ")";
  return "";
}

// One REST interaction. `doc` is the parsed body when it was JSON, `raw` the body
// as received; `error` is what the caller reports.
struct Rest {
  bool        ok{false};
  int         status{0};
  std::string error;
  json        doc;
  std::string raw;
};

// Failures Slurm reports inside a 2xx body: a rejected submit comes as errors[],
// a cancel that could not be done as status[].error.
std::string SlurmDetail(const json &doc)
{
  auto stringOf = [](const json &value, const char *key) -> std::string {
    return (value.contains(key) && value[key].is_string()) ? value[key].get<std::string>() : std::string();
  };
  if (doc.contains("errors") && doc["errors"].is_array()) {
    for (const auto &e : doc["errors"]) {
      const std::string d = stringOf(e, "description").empty() ? stringOf(e, "error") : stringOf(e, "description");
      if (!d.empty()) return d;
    }
  }
  if (doc.contains("status") && doc["status"].is_array()) {
    for (const auto &s : doc["status"]) {
      if (s.contains("error") && s["error"].is_object()) {
        const std::string m = stringOf(s["error"], "message");
        if (!m.empty()) return m;
      }
    }
  }
  return "";
}

Rest Call(const std::string &method, const std::string &path, const json &body = json(),
          const std::string &api = "/slurm/", bool failOnSlurmError = true)
{
  Rest r;

  const auto headers = Headers();
  if (headers.at("X-SLURM-USER-TOKEN").empty()) {
    r.error = "no Slurm token in " + TokenFile() + " (mount the room's token Secret)";
    return r;
  }

  Ndmspc::NHttpRequest http;
  if (gApiVersion.empty()) {
    gApiVersion = NegotiateApiVersion(http, headers, r.error);
    if (gApiVersion.empty()) return r;
  }

  const std::string url = RestUrl() + api + gApiVersion + "/" + path;
  try {
    const Ndmspc::NHttpResponse response = http.request(method, url, body.is_null() ? std::string() : body.dump(), headers);
    r.status = response.status;
    r.raw    = response.body;
  } catch (const std::exception &e) {
    r.error = std::string("slurmrestd request failed: ") + e.what() + " (" + url + ")";
    return r;
  }

  try {
    r.doc = json::parse(r.raw);
  } catch (...) {
    r.doc = json();
  }

  if (r.status < 200 || r.status >= 300) {
    r.error = "slurmrestd " + method + " " + url + " -> HTTP " + std::to_string(r.status);
    const std::string detail = SlurmDetail(r.doc);
    r.error += detail.empty() ? (": " + Trim(r.raw).substr(0, 300)) : (": " + detail);
    return r;
  }
  if (failOnSlurmError) {
    const std::string detail = SlurmDetail(r.doc);
    if (!detail.empty()) {
      r.error = detail;
      return r;
    }
  }

  r.ok = true;
  return r;
}

// slurmdbd endpoints only answer when slurmrestd was started with the slurmdbd
// OpenAPI plugin; remember a 404 so a job detail does not ask twice.
bool gAccountingUnavailable = false;

// sbatch time formats to the minutes time_limit wants: "5", "minutes:seconds",
// "hours:minutes:seconds", "days-hours", "days-hours:minutes",
// "days-hours:minutes:seconds". Returns -1 when it cannot be read, 0 to leave the
// partition's limit in place (as --time=UNLIMITED does).
int TimeToMinutes(const std::string &value)
{
  if (value.empty()) return 0;
  if (value == "UNLIMITED" || value == "unlimited") return 0;

  std::string rest = value;
  int         days = 0;
  const size_t dash = rest.find('-');
  if (dash != std::string::npos) {
    const std::string d = rest.substr(0, dash);
    if (d.empty() || d.find_first_not_of("0123456789") != std::string::npos) return -1;
    days = std::atoi(d.c_str());
    rest = rest.substr(dash + 1); // "2-" is two days with no hours
    if (Trim(rest).empty()) return days * 24 * 60;
  }

  std::vector<int> parts;
  size_t           start = 0;
  while (true) {
    const size_t      colon = rest.find(':', start);
    const std::string p     = rest.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
    if (p.empty() || p.find_first_not_of("0123456789") != std::string::npos) return -1;
    parts.push_back(std::atoi(p.c_str()));
    if (colon == std::string::npos) break;
    start = colon + 1;
  }
  if (parts.size() > 3) return -1;

  int minutes = 0;
  int seconds = 0;
  if (dash == std::string::npos) {
    // minutes | minutes:seconds | hours:minutes:seconds
    if (parts.size() == 1)      minutes = parts[0];
    else if (parts.size() == 2) { minutes = parts[0]; seconds = parts[1]; }
    else                        { minutes = parts[0] * 60 + parts[1]; seconds = parts[2]; }
  } else {
    // days-hours | days-hours:minutes | days-hours:minutes:seconds
    const int hours = parts[0];
    minutes = days * 24 * 60 + hours * 60;
    if (parts.size() >= 2) minutes += parts[1];
    if (parts.size() == 3) seconds = parts[2];
  }
  // time_limit is in minutes: round a remainder up rather than cutting the job short.
  if (seconds > 0) minutes += 1;
  return minutes;
}

} // namespace

void httpSlurm()
{
  auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);

  Ndmspc::RegisterMcpTool("slurm/submit", {
      .description = "Submit a Slurm batch job (runs ndmspc-run unless 'command'/'script' is given).",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"command", {{"type", "string"}}},
                        {"options", {{"type", "object"},
                                     {{"properties", {{"partition", {{"type", "string"}}},
                                                      {"time", {{"type", "string"}}},
                                                      {"array", {{"type", "string"}}},
                                                      {"jobName", {{"type", "string"}}},
                                                      {"extra", {{"type", "string"}}}}}}}}}}},
  });
  Ndmspc::RegisterMcpTool("slurm/jobs", "List the Slurm queue.");
  Ndmspc::RegisterMcpTool("slurm/job", "Inspect (GET) or cancel (DELETE) one Slurm job.");
  Ndmspc::RegisterMcpTool("slurm/nodes", "Show the Slurm partitions/nodes.");

  handlers["slurm/submit"] = [](std::string method, json &in, json &out, json &wsOut,
                                std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    // Side-effecting actions must never be replayed, or a room restore would
    // re-submit every job.
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    if (!ctx.IsPost()) {
      ctx.Error("POST required");
      return;
    }

    // NRouteContext::GetString is flat (it does fIn.contains(key)), so the nested
    // 'options' object has to be read explicitly.
    json opts = json::object();
    if (ctx.In().contains("options") && ctx.In()["options"].is_object()) opts = ctx.In()["options"];
    auto Opt = [&opts](const char *key) -> std::string {
      auto it = opts.find(key);
      return (it != opts.end() && it->is_string()) ? it->get<std::string>() : std::string();
    };

    const std::string partition =
        Opt("partition").empty() ? Env("NDMSPC_SLURM_DEFAULT_PARTITION", "all") : Opt("partition");
    const std::string time    = Opt("time");
    const std::string array   = Opt("array");
    const std::string jobName = Opt("jobName");
    const std::string extra   = Opt("extra");
    const std::string payload = ctx.GetString("command", ctx.GetString("script", ""));

    for (const auto &v : {partition, array, jobName}) {
      if (!v.empty() && !Safe(v, 128)) {
        ctx.Error("invalid value in 'options'");
        return;
      }
    }
    // sbatch flags have no REST equivalent: the request names the fields instead.
    if (!extra.empty()) {
      ctx.Error("'extra' (raw sbatch flags) is not supported over the REST API, use the named options");
      return;
    }
    if (payload.empty()) {
      ctx.Error("'command' (or 'script') is required");
      return;
    }

    const int minutes = TimeToMinutes(time);
    if (minutes < 0) {
      ctx.Error("invalid 'time' (use minutes, HH:MM:SS or D-HH:MM:SS)");
      return;
    }

    // The job runs as the account the token belongs to (SlurmUser), on a compute
    // node whose image has no home for it, so it runs in /tmp — the same place the
    // client used to use before submitting over REST.
    const std::string jobCommand = Env("NDMSPC_SLURM_RUN", "/usr/bin/ndmspc-run") + " " + payload;
    json              job        = json::object();
    if (!jobName.empty()) job["name"] = jobName;
    if (!partition.empty()) job["partition"] = partition;
    if (!array.empty()) job["array"] = array;
    if (minutes > 0) job["time_limit"] = {{"set", true}, {"number", minutes}};
    job["tasks"]                     = 1;
    job["current_working_directory"] = "/tmp";
    job["environment"]               = {"PATH=/usr/local/bin:/usr/bin:/bin", "HOME=/tmp"};

    // --wrap-equivalent: the REST API takes the batch script itself.
    const json body = {{"script", "#!/bin/bash\n" + jobCommand + "\n"}, {"job", job}};

    const Rest r = Call("POST", "job/submit", body);
    if (!r.ok) {
      ctx.Error("submit failed: " + r.error);
      return;
    }

    out["jobId"]   = std::to_string(r.doc.value("job_id", 0));
    out["command"] = jobCommand;
    ctx.Success();
  };

  handlers["slurm/jobs"] = [](std::string method, json &in, json &out, json &wsOut,
                              std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    // A view, not an action: report what came back and let the caller decide,
    // rather than failing the request.
    const Rest r   = Call("GET", "jobs");
    out["jobList"] = (r.ok && r.doc.contains("jobs")) ? r.doc["jobs"] : json::array();
    if (!r.ok) out["error"] = r.error;
    ctx.Success();
  };

  handlers["slurm/job"] = [](std::string method, json &in, json &out, json &wsOut,
                             std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    const std::string id = ctx.GetString("id", "");
    if (!Safe(id, 32) || id.find_first_not_of("0123456789") != std::string::npos) {
      ctx.Error("'id' is required");
      return;
    }

    if (ctx.IsDelete()) {
      // Cancelling is an action: a job that could not be cancelled is an error.
      const Rest r = Call("DELETE", "job/" + id);
      out["output"] = r.error.empty() ? std::string("cancelled " + id) : r.error;
      if (r.ok) {
        ctx.Success();
      } else {
        ctx.Error("scancel failed: " + r.error);
      }
      return;
    }

    const Rest r = Call("GET", "job/" + id);
    if (r.ok) {
      out["job"] = r.doc;
    } else {
      out["error"] = r.error;
    }

    // Accounting (sacct's equivalent) lives on the slurmdbd endpoints, which only
    // exist when slurmrestd loads that plugin: ask once, then stop trying.
    if (!gAccountingUnavailable) {
      const Rest a = Call("GET", "job/" + id, json(), "/slurmdb/", false);
      if (a.ok) {
        out["accounting"] = a.doc;
      } else if (a.status == 404) {
        gAccountingUnavailable = true;
      }
    }
    ctx.Success();
  };

  handlers["slurm/nodes"] = [](std::string method, json &in, json &out, json &wsOut,
                               std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    const Rest nodes      = Call("GET", "nodes");
    const Rest partitions = Call("GET", "partitions");
    out["nodes"]      = (nodes.ok && nodes.doc.contains("nodes")) ? nodes.doc["nodes"] : json::array();
    out["partitions"] = (partitions.ok && partitions.doc.contains("partitions")) ? partitions.doc["partitions"]
                                                                                 : json::array();
    if (!nodes.ok) out["error"] = nodes.error;
    ctx.Success();
  };
}
