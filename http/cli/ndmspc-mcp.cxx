///
/// ndmspc-mcp — standalone Model Context Protocol (MCP) server for the NGnTree
/// HTTP handlers, speaking JSON-RPC 2.0 over stdio.
///
/// It embeds an NGnHttpServer (without starting the network engine), loads the
/// same built-in macros as `ndmspc-server start ngnt`, and exposes every
/// registered handler as an MCP tool through Ndmspc::NMcpServer. This lets an
/// MCP client (launched as a subprocess) drive ngnt with no separately running
/// HTTP server.
///
/// The real stdout carries ONLY JSON-RPC messages; every other write (logger,
/// ROOT, printf from macros) is redirected to stderr.
///
/// Usage:
///   ndmspc-mcp [-m <macros>] [--all-tools] [-v]
///

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include <unistd.h>

#include <CLI/CLI.hpp>
#include <TApplication.h>
#include <TROOT.h>
#include <TSystem.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/http/NGnHttpServer.h"
#include "ndmspc/http/NMcpServer.h"
#include "ndmspc/ndmspc.h"

namespace {

/// @brief File stream bound to the original stdout, reserved for JSON-RPC.
FILE * gMcpOut = nullptr;

/// @brief Duplicate stdout for JSON-RPC, then point fd 1 at stderr so that any
///        other output (logger, ROOT, macro printf/Print) cannot corrupt the
///        protocol stream.
void RedirectStdoutToStderr()
{
  std::fflush(stdout);
  const int saved = ::dup(STDOUT_FILENO);
  gMcpOut         = (saved >= 0) ? ::fdopen(saved, "w") : nullptr;
  if (::dup2(STDERR_FILENO, STDOUT_FILENO) < 0 && gMcpOut == nullptr) {
    // Fall back to the original stdout if we could not redirect; better noisy
    // than silent.
    gMcpOut = stdout;
  }
  if (gMcpOut == nullptr) gMcpOut = stdout;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
}

void WriteResponse(const json & response)
{
  if (response.is_null() || gMcpOut == nullptr) return; // notifications have no reply
  const std::string text = response.dump();
  std::fwrite(text.data(), 1, text.size(), gMcpOut);
  std::fputc('\n', gMcpOut);
  std::fflush(gMcpOut);
}

std::string AppDescription()
{
  return std::string(NDMSPC_NAME) + " v" + NDMSPC_VERSION + "-" + NDMSPC_VERSION_RELEASE + " (mcp)";
}

std::string AppVersion()
{
  return std::string(NDMSPC_NAME) + " v" + NDMSPC_VERSION + "-" + NDMSPC_VERSION_RELEASE;
}

/// @brief Directory that holds the installed macros (mirrors ndmspc-server).
std::string NdmspcDir()
{
  const char * env = gSystem->Getenv("NDMSPC_DIR");
  std::string  dir = (env && *env) ? env : "";
  if (dir.empty()) {
    const char * env2 = gSystem->Getenv("NDMSPC__HOME");
    dir               = (env2 && *env2) ? env2 : "/usr/share/ndmspc";
  }
  return dir;
}

} // namespace

int main(int argc, char ** argv)
{
  std::string macroFilename;
  std::string transport = "stdio";
  bool        allTools  = false;
  bool        verbose   = false;
  bool        withRooms = false;
  if (const char * roomsEnv = std::getenv("NDMSPC_ROOMS"); roomsEnv != nullptr && *roomsEnv != '\0') {
    withRooms = Ndmspc::NUtils::ParseBoolEnv(roomsEnv);
  }

  CLI::App app{AppDescription()};
  app.set_version_flag("--version", AppVersion(), "Print version information and exit");
  app.add_option("-m,--macro", macroFilename,
                 "Macro path list separated by commas (default: "
                 "$NDMSPC_DIR/macros/builtin/httpNgntBase.C,$NDMSPC_DIR/macros/builtin/httpNgnt.C)");
  app.add_option("--transport", transport, "Transport to serve (default: stdio)")
      ->check(CLI::IsMember({"stdio"}));
  app.add_flag("--all-tools", allTools, "Also expose internal actions (debug, openapi) as tools");
  app.add_flag("--rooms", withRooms,
               "Also load the room router macro (macros/builtin/httpRoom.C) and expose the room_* tools; "
               "disabled by default (--rooms or NDMSPC_ROOMS=1). Kubernetes only: the process exits when "
               "KUBERNETES_SERVICE_HOST is unset");
  app.add_flag("-v,--verbose", verbose, "Enable verbose logging on stderr");
  CLI11_PARSE(app, argc, argv);

  // Must be set before TApplication is constructed (see ndmspc-run/ndmspc-worker):
  // otherwise TApplication connects to the real X server. The MCP server is headless.
  gROOT->SetBatch(kTRUE);

  // Reserve the original stdout for JSON-RPC before anything else can print.
  RedirectStdoutToStderr();

  if (verbose && !gSystem->Getenv("NDMSPC_LOG_CONSOLE")) {
    Ndmspc::NLogger::SetConsoleOutput(true);
  }
  (void)transport; // only "stdio" is supported for now

  TApplication rootApp("ndmspc-mcp", 0, nullptr);

  if (macroFilename.empty()) {
    const std::string dir = NdmspcDir();
    if (gSystem->AccessPathName(dir.c_str()) != 0) {
      NLogError("No macro file given and default directory '%s' not found. Provide one with -m.", dir.c_str());
      return 1;
    }
    macroFilename = dir + "/macros/builtin/httpNgntBase.C," + dir + "/macros/builtin/httpNgnt.C";
  }

  if (withRooms) {
    const std::string roomMacro = NdmspcDir() + "/macros/builtin/httpRoom.C";
    if (macroFilename.find(roomMacro) == std::string::npos) {
      macroFilename += "," + roomMacro;
      NLogInfo("ndmspc-mcp: rooms enabled, loading '%s'", roomMacro.c_str());
    }
  }

  auto server = std::make_unique<Ndmspc::NGnHttpServer>(/*engine=*/"", /*ws=*/true, /*heartbeat_ms=*/10000,
                                                        Ndmspc::NOidcConfig{}, /*startEngine=*/false);

  std::map<std::string, Ndmspc::NGnHttpFuncPtr> handlers;
  Ndmspc::gNdmspcHttpHandlers = &handlers;
  // MCP tool metadata declared by the macros (descriptions, allowed verbs, ...)
  Ndmspc::NMcpToolMap mcpTools;
  Ndmspc::gNdmspcMcpTools = &mcpTools;

  for (const auto & macro : Ndmspc::NUtils::Tokenize(macroFilename, ',')) {
    NLogInfo("ndmspc-mcp: loading macro '%s'", macro.c_str());
    TMacro * m = Ndmspc::NUtils::OpenMacro(macro);
    if (m == nullptr) {
      NLogError("ndmspc-mcp: failed to open macro '%s'", macro.c_str());
      return 1;
    }
    m->Exec();
  }
  server->SetHttpHandlers(handlers);
  NLogInfo("ndmspc-mcp: %zu action(s) registered, serving MCP over stdio", handlers.size());

  Ndmspc::NMcpServer::Options options;
  if (allTools) options.excludeActions.clear();
  Ndmspc::NMcpServer mcp(server.get(), options);

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    WriteResponse(mcp.HandleText(line));
  }

  return 0;
}
