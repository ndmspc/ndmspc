///
/// httpSlurm.C — HTTP/MCP interface to the cluster's Slurm.
/// Registers: slurm/submit, slurm/jobs, slurm/job, slurm/nodes
/// URLs:      POST /api/slurm/submit, GET /api/slurm/jobs,
///            GET|POST|DELETE /api/slurm/job, GET /api/slurm/nodes
///
/// slurm/job takes the job id in the request body ({"id":"42"}): the URL query is
/// reserved for the room router's ?room=<id>.
///
/// Job output stays on the compute node a job ran on, so a room does not see it; a
/// log push through a Slurm epilog was removed and will be redone later.
///
/// Slurm is the source of truth: the handlers shell out to the Slurm clients and
/// return their JSON. There is no local job registry (macros/builtin/mon is
/// deprecated and unused).
///
/// Usage: ndmspc-server -m "<dir>/httpNgntBase.C,<dir>/httpNgnt.C,<dir>/httpSlurm.C"
///
/// The pod needs the Slurm client tools and the cluster's /etc/slurm
/// (slurm.conf + slurm.key for AuthType=auth/slurm). Overridables:
///   NDMSPC_SLURM_{SBATCH,SQUEUE,SCONTROL,SACCT,SCANCEL,SINFO}
///   NDMSPC_SLURM_RUN (default /usr/bin/ndmspc-run)
///   NDMSPC_SLURM_DEFAULT_PARTITION (default "all")

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <sys/wait.h>

#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NRouteContext.h>

namespace {

std::string Env(const char *name, const char *def)
{
  const char *v = std::getenv(name);
  return (v && *v) ? std::string(v) : std::string(def);
}

// Anything that reaches a shell command must not carry metacharacters.
bool Safe(const std::string &s, size_t max = 512)
{
  return !s.empty() && s.size() <= max && s.find_first_of(";|&$`><\n\\\"'()") == std::string::npos;
}

std::string Quote(const std::string &s)
{
  std::string q = "'";
  for (char c : s) q += (c == '\'') ? "'\\''" : std::string(1, c);
  return q + "'";
}

std::string Run(const std::string &cmd, int &rc)
{
  std::string out;
  FILE *p = popen((cmd + " 2>&1").c_str(), "r");
  if (!p) {
    rc = -1;
    return out;
  }
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), p)) out += buf;
  rc = pclose(p);
  if (WIFEXITED(rc)) rc = WEXITSTATUS(rc);
  return out;
}

// `sbatch --parsable` prints "<id>" or "<id>;<cluster>"; older output is
// "Submitted batch job <id>". Either way the id is the first numeric run.
std::string JobIdFrom(const std::string &sbatchOutput)
{
  for (size_t i = 0; i < sbatchOutput.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(sbatchOutput[i]))) continue;
    size_t e = i;
    while (e < sbatchOutput.size() && std::isdigit(static_cast<unsigned char>(sbatchOutput[e]))) ++e;
    return sbatchOutput.substr(i, e - i);
  }
  return "";
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
  Ndmspc::RegisterMcpTool("slurm/jobs", "List the Slurm queue (squeue).");
  Ndmspc::RegisterMcpTool("slurm/job", "Inspect (GET) or cancel (DELETE) one Slurm job.");
  Ndmspc::RegisterMcpTool("slurm/nodes", "Show the Slurm partitions/nodes (sinfo).");

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

    for (const auto &v : {partition, time, array, jobName, extra}) {
      if (!v.empty() && !Safe(v, 128)) {
        ctx.Error("invalid value in 'options'");
        return;
      }
    }
    if (payload.empty()) {
      ctx.Error("'command' (or 'script') is required");
      return;
    }

    std::string cmd = Env("NDMSPC_SLURM_SBATCH", "sbatch") + " --parsable";
    if (!partition.empty()) cmd += " --partition=" + partition;
    if (!time.empty()) cmd += " --time=" + time;
    if (!array.empty()) cmd += " --array=" + array;
    if (!jobName.empty()) cmd += " --job-name=" + jobName;
    if (!extra.empty()) cmd += " " + extra;
    // --wrap and not `sbatch <run> <args>`: sbatch takes its first non-option
    // argument as a batch script *file* and rejects anything without a #! line
    // ("This does not look like a batch script"), while ndmspc-run is a binary.
    // --wrap makes sbatch generate the script around the command instead.
    cmd += " --wrap=" + Quote(Env("NDMSPC_SLURM_RUN", "/usr/bin/ndmspc-run") + " " + payload);

    int         rc  = 0;
    std::string txt = Run(cmd, rc);
    if (rc != 0) {
      ctx.Error("sbatch failed: " + txt);
      out["command"] = cmd;
      return;
    }

    out["jobId"]   = JobIdFrom(txt);
    out["command"] = cmd;
    out["output"]  = txt;
    ctx.Success();
  };

  handlers["slurm/jobs"] = [](std::string method, json &in, json &out, json &wsOut,
                             std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    int               rc      = 0;
    const std::string jsonOut = Run(Env("NDMSPC_SLURM_SQUEUE", "squeue") + " --json", rc);
    if (rc == 0) {
      try {
        out["jobList"] = json::parse(jsonOut)["jobs"];
      } catch (...) {
        out["raw"] = jsonOut;
      }
    } else {
      out["raw"] = Run(Env("NDMSPC_SLURM_SQUEUE", "squeue"), rc);
    }
    ctx.Success();
  };

  handlers["slurm/job"] = [](std::string method, json &in, json &out, json &wsOut,
                            std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    const std::string id = ctx.GetString("id", "");
    if (!Safe(id, 32)) {
      ctx.Error("'id' is required");
      return;
    }

    int rc = 0;
    if (ctx.IsDelete()) {
      const std::string txt = Run(Env("NDMSPC_SLURM_SCANCEL", "scancel") + " " + Quote(id), rc);
      out["output"] = txt;
      // Result() is error-only in NRouteContext; use Success()/Error().
      if (rc == 0) {
        ctx.Success();
      } else {
        ctx.Error("scancel failed: " + txt);
      }
      return;
    }

    const std::string info =
        Run(Env("NDMSPC_SLURM_SCONTROL", "scontrol") + " show job " + Quote(id) + " --json", rc);
    if (rc == 0) {
      try {
        out["job"] = json::parse(info);
      } catch (...) {
        out["raw"] = info;
      }
    } else {
      out["raw"] = info;
    }

    const std::string acct =
        Run(Env("NDMSPC_SLURM_SACCT", "sacct") + " -j " + Quote(id) + " --json", rc);
    if (rc == 0) {
      try {
        out["accounting"] = json::parse(acct);
      } catch (...) {
        out["accountingRaw"] = acct;
      }
    } else {
      out["accountingRaw"] = acct;
    }
    ctx.Success();
  };

  handlers["slurm/nodes"] = [](std::string method, json &in, json &out, json &wsOut,
                              std::map<std::string, TObject *> &objects) {
    Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
    if (ctx.Server()) ctx.Server()->SetUseHistory(false);
    int               rc      = 0;
    const std::string jsonOut = Run(Env("NDMSPC_SLURM_SINFO", "sinfo") + " --json", rc);
    if (rc == 0) {
      try {
        // sinfo --json returns the nodes under "sinfo" (Slurm >= 23.11) and the
        // partitions under "partitions".
        auto j            = json::parse(jsonOut);
        out["nodes"]      = j.contains("sinfo") ? j["sinfo"] : json();
        out["partitions"] = j.contains("partitions") ? j["partitions"] : json();
      } catch (...) {
        out["raw"] = jsonOut;
      }
    } else {
      out["raw"] = Run(Env("NDMSPC_SLURM_SINFO", "sinfo"), rc);
    }
    ctx.Success();
  };
}
