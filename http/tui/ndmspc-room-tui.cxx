// ndmspc-room-tui - terminal UI and scriptable client for the ndmspc room router.
//
// The router is an ngnt server with the framework's room router enabled (Ndmspc::NRoomRouter):
// one Knative Service per room, created on demand, with clients carrying their room in
// the ?room=<id> query parameter. This tool drives the router's room actions over its
// MCP endpoint (POST {url}/api/mcp, tools room_list/room_open/room_status/room_close),
// which dispatch to the same handlers as the /api/room routes.
//
// Interactive use runs the screen in room_ui.cxx; the --list/--open/--status/--close
// actions print JSON and exit without a terminal.
//
// This file deliberately includes no ROOT-free assumption either way: ROOT is linked in
// through NdmspcHttp (TBase64 is used to materialise a passphrase file), and the FTXUI
// screen lives in its own translation unit.
#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <TBase64.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/http/NKeyPassphrase.h"
#include "ndmspc/http/NOidcTokenClient.h"
#include "ndmspc/http/NRoomClient.h"

#include "room_ui.h"

namespace {

std::string app_description()
{
  return "ndmspc-room-tui - terminal UI for the ndmspc room router";
}

/// @brief One action requested on the command line (non-interactive use).
struct PendingAction {
  std::string name;  ///< "list", "open", "status", "close", "backup" or "restore"
  std::string value; ///< Room id, or the file for "backup"/"restore" (empty for "list")
};

/// @brief A private, base64-encoded passphrase file that removes itself on exit.
///
/// NHttpRequest reads the key passphrase from a base64 file, while the passphrase may
/// arrive inline (--key-pass / NDMSPC_KEY_PASS) or from an interactive prompt, so it is
/// materialised here for the lifetime of the run. mkstemp creates it 0600, and it is
/// removed as soon as the process ends.
class KeyPassphraseFile {
  public:
  KeyPassphraseFile() = default;
  KeyPassphraseFile(const KeyPassphraseFile &)            = delete;
  KeyPassphraseFile & operator=(const KeyPassphraseFile &) = delete;

  ~KeyPassphraseFile()
  {
    if (!fPath.empty() && std::remove(fPath.c_str()) != 0) {
      NLogWarning("Could not remove the temporary key passphrase file %s", fPath.c_str());
    }
  }

  /// @brief Write the passphrase to a fresh 0600 file.
  /// @param passphrase The resolved passphrase.
  /// @return The file path, or "" on failure.
  std::string Write(const std::string & passphrase)
  {
    char templatePath[] = "/tmp/ndmspc-room-tui-keypass.XXXXXX";
    const int fd        = ::mkstemp(templatePath);
    if (fd < 0) {
      NLogError("Cannot create a temporary file for the client key passphrase");
      return {};
    }
    fPath = templatePath;

    const std::string encoded = TBase64::Encode(passphrase.c_str(), passphrase.size()).Data();
    const std::string content = encoded + "\n";
    const ssize_t     written = ::write(fd, content.data(), content.size());
    ::close(fd);

    if (written != static_cast<ssize_t>(content.size())) {
      NLogError("Cannot write the temporary key passphrase file %s", fPath.c_str());
      return {};
    }
    return fPath;
  }

  private:
  std::string fPath;
};

/// @brief Whether a handshake failure is worth retrying.
///
/// Only a router that is not answering yet is worth waiting for. A rejected
/// certificate, a wrong URL or an authentication failure is identical on every attempt,
/// so it must fail immediately rather than after the retry budget.
bool IsTransient(const std::string & error) { return error.find("cannot reach") != std::string::npos; }

/// @brief Print a payload as indented JSON on stdout.
void PrintJson(const json & payload) { std::cout << payload.dump(2) << std::endl; }

/// @brief Follows a room the router is preparing until it is ready, or the wait runs out.
///
/// --open used to block inside the router until the room was ready. The router now creates rooms
/// in the background (so it stays usable while one is being prepared), which moves the wait here -
/// the URL it prints is only usable once the room is ready.
///
/// @param client The room client (already handshaken).
/// @param room Room id to follow.
/// @param timeoutSeconds How long to follow it before giving up.
/// @return The room's final status, or a failure when it could not be created or timed out.
Ndmspc::NRoomResult WaitForRoom(Ndmspc::NRoomClient & client, const std::string & room, int timeoutSeconds)
{
  const auto  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
  std::string phase;
  bool        announced = false;

  while (true) {
    const Ndmspc::NRoomResult status = client.Status(room);
    if (!status.ok) return status;

    const std::string state = status.payload.value("state", std::string());
    if (state != "preparing") {
      if (state != "failed") return status;
      Ndmspc::NRoomResult failure;
      failure.error = "the room '" + room + "' could not be created: " +
                      status.payload.value("error", std::string("no reason reported"));
      return failure;
    }

    // Progress goes to stderr: on stdout a script expects the action's JSON and nothing else.
    const std::string next = status.payload.value("phase", std::string());
    if (!announced || next != phase) {
      std::cerr << "creating '" << room << "': " << (next.empty() ? "waiting" : next) << std::endl;
      announced = true;
    }
    phase = next;

    if (std::chrono::steady_clock::now() >= deadline) {
      Ndmspc::NRoomResult timeout;
      timeout.error = "the room '" + room + "' is still being created after " + std::to_string(timeoutSeconds) +
                      "s (phase '" + phase + "'); it continues in the router - check it with --status";
      return timeout;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

/// @brief Run one room action without a terminal.
/// @param client The room client (already handshaken).
/// @param action The action to run.
/// @param force Allow --backup to overwrite an existing file.
/// @param noWait With --open: return as soon as the router accepts the room.
/// @param waitTimeoutSeconds How long --open follows a room the router is still preparing.
/// @return The process exit code: 0 on success, 1 on failure.
int RunHeadless(Ndmspc::NRoomClient & client, const PendingAction & action, bool force, bool noWait,
                int waitTimeoutSeconds)
{
  if (action.name == "list") {
    const Ndmspc::NRoomListResult list = client.List();
    if (!list.ok) {
      NLogError("%s", list.error.c_str());
      return 1;
    }
    json rooms = json::array();
    for (const auto & room : list.rooms) {
      rooms.push_back({{"name", room.name},         {"room", room.room},         {"revision", room.revision},
                       {"lastSeen", room.lastSeen}, {"ready", room.ready},       {"replicas", room.replicas},
                       {"active", room.active},     {"state", room.state},       {"preparing", room.preparing},
                       {"phase", room.phase},       {"error", room.error},       {"code", room.code},
                       {"startedAt", room.startedAt}});
    }
    PrintJson({{"rooms", rooms}, {"ttl", list.ttl}});
    return 0;
  }

  // Export every room and its session to a file. Refusing to overwrite by default: a backup
  // that silently clobbers the last good one is worse than an error.
  if (action.name == "backup") {
    if (!force) {
      std::ifstream existing(action.value);
      if (existing.good()) {
        NLogError("Refusing to overwrite %s; pass --force to replace it", action.value.c_str());
        return 1;
      }
    }
    const Ndmspc::NRoomResult result = client.Backup();
    if (!result.ok) {
      NLogError("%s", result.error.c_str());
      return 1;
    }
    std::ofstream out(action.value);
    if (!out.is_open()) {
      NLogError("Cannot write %s", action.value.c_str());
      return 1;
    }
    out << result.payload.dump(2) << std::endl;
    NLogInfo("Wrote %zu room(s) to %s", result.payload.value("rooms", json::array()).size(), action.value.c_str());
    return 0;
  }

  // Re-create the rooms named in a document and replay their sessions.
  if (action.name == "restore") {
    std::ifstream in(action.value);
    if (!in.is_open()) {
      NLogError("Cannot read %s", action.value.c_str());
      return 1;
    }
    json document;
    try {
      in >> document;
    }
    catch (const json::parse_error & e) {
      NLogError("%s is not valid JSON: %s", action.value.c_str(), e.what());
      return 1;
    }

    const Ndmspc::NRoomResult result = client.Restore(document);
    if (!result.ok) {
      NLogError("%s", result.error.c_str());
      return 1;
    }

    const json restored = result.payload.value("restored", json::array());
    const json failed   = result.payload.value("failed", json::array());
    for (const auto & room : restored) {
      const std::string session = room.value("session", "");
      NLogInfo("  %s (%s)%s", room.value("room", "").c_str(), room.value("revision", "").c_str(),
               session == "live" ? " - already in use, left alone" : "");
    }
    for (const auto & room : failed) {
      NLogError("  %s: %s", room.value("room", "").c_str(), room.value("error", "").c_str());
    }
    NLogInfo("Restored %zu room(s), %zu failed", restored.size(), failed.size());
    return failed.empty() ? 0 : 1;
  }

  Ndmspc::NRoomResult result;
  if (action.name == "open") {
    // Never wait inside the router: it would hold up every other client for the whole creation.
    result = client.Open(action.value, /*wait=*/false);
    if (result.ok) {
      const std::string state = result.payload.value("state", std::string());
      if (state == "failed") {
        // The router answered with the failure already recorded (it gives up as soon as it knows,
        // e.g. a pod the scheduler cannot place): say so instead of printing a room that is not there.
        Ndmspc::NRoomResult failure;
        failure.error = "the room '" + action.value + "' could not be created: " +
                        result.payload.value("error", std::string("no reason reported"));
        result = failure;
      }
      else if (!noWait && state == "preparing") {
        const Ndmspc::NRoomResult waited = WaitForRoom(client, action.value, waitTimeoutSeconds);
        if (!waited.ok) {
          result = waited; // timed out, or the creation failed: report it and exit non-zero
        }
        else {
          // Keep the payload room/open returned - it carries the URL, which a status does not - and
          // take the outcome the router reported once the room was up.
          result.payload["state"]    = waited.payload.value("state", std::string("ready"));
          const std::string revision = result.payload.value("revision", std::string());
          result.payload["revision"] = waited.payload.value("revision", revision);
          if (waited.payload.contains("session")) result.payload["session"] = waited.payload["session"];
        }
      }
    }
  }
  else if (action.name == "status") {
    result = client.Status(action.value);
  }
  else {
    result = client.Close(action.value);
  }

  if (!result.ok) {
    NLogError("%s", result.error.c_str());
    return 1;
  }
  PrintJson(result.payload);
  return 0;
}

} // namespace

int main(int argc, char ** argv)
{
  std::string serverUrl      = "http://localhost:8080";
  int         refreshSeconds = 5;
  bool        listRooms      = false;
  std::string openRoom;
  std::string statusRoom;
  std::string closeRoom;
  std::string backupFile;
  std::string restoreFile;
  bool        force            = false;
  bool        noWait           = false;
  int         waitTimeoutSeconds = 600;

  std::string certFile;
  std::string keyFile;
  std::string keyPassword;
  std::string keyPasswordFile;
  std::string caFile;
  std::string caPath;
  bool        allowInsecure = false;

  std::string oidcIssuer;
  std::string oidcClientId;
  std::string oidcClientSecret;
  std::string oidcUsername;
  std::string oidcPassword;
  std::string oidcGrant = "client_credentials";
  std::string oidcCaFile;
  std::string oidcCaPath;
  bool        oidcAllowInsecureHttp = false;
  int         oidcTokenRefresh      = 300;
  int         connectRetries        = 3;

  CLI::App app{app_description()};
  app.set_version_flag("--version", "ndmspc-room-tui", "Print version information and exit");
  app.set_help_all_flag("--help-all", "Expand all help");

  app.add_option("--url,-u", serverUrl, "Room router base URL, or a full .../api/mcp endpoint "
                                        "(default: http://localhost:8080)")
      ->envname("NDMSPC_ROOM_URL");
  app.add_option("--refresh,-r", refreshSeconds, "Seconds between automatic room-list refreshes (0 = manual only)");
  app.add_flag("--list", listRooms, "List the rooms the router is tracking, then exit (no terminal needed)");
  app.add_option("--open", openRoom,
                 "Ensure a room exists, print its URL, then exit (no terminal needed). Waits for the room "
                 "to be ready unless --no-wait is given");
  app.add_flag("--no-wait", noWait,
               "With --open: return as soon as the router accepts the room, without waiting for it to be ready");
  app.add_option("--wait-timeout", waitTimeoutSeconds,
                 "With --open: seconds to wait for the room to become ready (default: 600)");
  app.add_option("--status", statusRoom, "Print one room's state, then exit (no terminal needed)");
  app.add_option("--close", closeRoom, "Delete a room, then exit (no terminal needed)");
  app.add_option("--backup", backupFile,
                 "Export every room and its session to this JSON file, then exit (no terminal needed)");
  app.add_option("--restore", restoreFile,
                 "Ensure and replay every room in a file written by --backup, then exit (no terminal needed)");
  app.add_flag("--force", force, "Allow --backup to overwrite an existing file");

  app.add_option("--cert", certFile, "Client certificate (PEM) for mutual TLS (X509)");
  app.add_option("--key", keyFile, "Client private key (PEM) for mutual TLS (X509)");
  app.add_option("--key-pass", keyPassword, "Password for the client private key")
      ->envname("NDMSPC_KEY_PASS");
  app.add_option("--key-pass-file", keyPasswordFile,
                 "Base64-encoded file holding the private-key password (as ~/.globus/password.txt)")
      ->envname("NDMSPC_KEY_PASS_FILE");
  app.add_option("--ca-file", caFile, "CA bundle used to verify the server certificate");
  app.add_option("--ca-path", caPath, "CA directory (hashed certificates) used to verify the server certificate");
  app.add_flag("--allow-insecure", allowInsecure, "Do not verify the server certificate (self-signed test servers)");

  app.add_option("--oidc-issuer", oidcIssuer, "OIDC issuer URL, e.g. https://keycloak/realms/ndmspc");
  app.add_option("--oidc-client-id", oidcClientId, "OIDC client identifier");
  app.add_option("--oidc-client-secret", oidcClientSecret, "OIDC client secret (optional for public clients)");
  app.add_option("--oidc-grant", oidcGrant, "OAuth2 grant: 'client_credentials' (default) or 'password'")
      ->check(CLI::IsMember({"client_credentials", "password"}));
  app.add_option("--oidc-username", oidcUsername, "Resource owner username (password grant only)");
  app.add_option("--oidc-password", oidcPassword, "Resource owner password (password grant only)");
  app.add_option("--oidc-ca-file", oidcCaFile, "CA bundle used to verify the OIDC issuer's TLS certificate");
  app.add_option("--oidc-ca-path", oidcCaPath,
                 "CA directory (hashed certificates) used to verify the OIDC issuer's TLS certificate");
  app.add_flag("--oidc-allow-insecure-http", oidcAllowInsecureHttp, "Allow an http:// OIDC issuer (development only)");
  app.add_option("--oidc-token-refresh", oidcTokenRefresh,
                 "Seconds before the OIDC access token is re-obtained (default: 300)");
  app.add_option("--connect-retries", connectRetries,
                 "Attempts before giving up on a router that is not answering yet (default: 3)");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError & e) {
    return app.exit(e);
  }

  std::vector<PendingAction> pending;
  if (listRooms) pending.push_back({"list", ""});
  if (!openRoom.empty()) pending.push_back({"open", openRoom});
  if (!statusRoom.empty()) pending.push_back({"status", statusRoom});
  if (!closeRoom.empty()) pending.push_back({"close", closeRoom});
  if (!backupFile.empty()) pending.push_back({"backup", backupFile});
  if (!restoreFile.empty()) pending.push_back({"restore", restoreFile});

  if (pending.size() > 1) {
    NLogError("Use at most one of --list, --open, --status, --close, --backup and --restore.");
    return 2;
  }
  if (serverUrl.empty()) {
    NLogError("--url is empty; give the room router's URL, e.g. http://localhost:8080");
    return 2;
  }

  Ndmspc::NLogger::SetProcessName("ndmspc-room-tui");

  // Resolve the client-key passphrase before anything owns the terminal: an interactive
  // prompt has to happen while the normal screen is still in use.
  KeyPassphraseFile keyPassphraseFile;
  std::string       keyPasswordFileForClient = keyPasswordFile;
  if (!keyFile.empty() && keyPasswordFile.empty()) {
    std::string resolved;
    if (!Ndmspc::NKeyPassphrase::Resolve(keyFile, keyPassword, "", resolved)) return 2;
    if (!resolved.empty()) {
      keyPasswordFileForClient = keyPassphraseFile.Write(resolved);
      if (keyPasswordFileForClient.empty()) return 2;
    }
  }

  Ndmspc::NOidcTokenClientConfig oidc;
  oidc.issuer            = oidcIssuer;
  oidc.clientId          = oidcClientId;
  oidc.clientSecret      = oidcClientSecret;
  oidc.username          = oidcUsername;
  oidc.password          = oidcPassword;
  oidc.grant             = oidcGrant == "password" ? Ndmspc::NOidcTokenClientConfig::Grant::Password
                                                   : Ndmspc::NOidcTokenClientConfig::Grant::ClientCredentials;
  oidc.caFile            = oidcCaFile;
  oidc.caPath            = oidcCaPath;
  oidc.allowInsecureHttp = oidcAllowInsecureHttp;

  if (oidc.Enabled()) {
    try {
      oidc.Normalize();
      oidc.Validate();
    }
    catch (const std::exception & e) {
      NLogError("Invalid OIDC configuration: %s", e.what());
      return 2;
    }
  }
  else if (!oidcIssuer.empty() || !oidcClientId.empty()) {
    NLogError("OIDC authentication requires both --oidc-issuer and --oidc-client-id");
    return 2;
  }

  std::string bearerToken;
  if (oidc.Enabled()) {
    try {
      Ndmspc::NOidcTokenClient tokenClient(oidc);
      bearerToken = tokenClient.ObtainAccessToken();
      NLogInfo("Obtained an OIDC access token (%zu bytes)", bearerToken.size());
    }
    catch (const std::exception & e) {
      NLogError("Failed to obtain an OIDC access token: %s", e.what());
      return 2;
    }
  }

  const std::string endpoint = Ndmspc::NRoomClient::McpEndpoint(serverUrl);
  Ndmspc::NRoomClient client(endpoint, bearerToken,
                             std::make_shared<Ndmspc::NRoomHttpClientImpl>(
                                 certFile, keyFile, keyPasswordFileForClient, caFile, caPath, allowInsecure));

  // Validate the URL and the MCP handshake up front, so a wrong address or a server
  // without the MCP endpoint is reported before a screen is started. Only a router that
  // is not answering yet is retried.
  std::string handshakeError;
  bool        handshakeOk = false;
  for (int attempt = 1; attempt <= std::max(1, connectRetries); ++attempt) {
    handshakeOk = client.Initialize(handshakeError);
    if (handshakeOk) break;
    if (!IsTransient(handshakeError) || attempt == std::max(1, connectRetries)) break;
    NLogWarning("Room router not answering yet (%s); retrying", handshakeError.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  if (!handshakeOk) {
    NLogError("Cannot use the room router's MCP endpoint: %s", handshakeError.c_str());
    NLogError("Router URL: %s", serverUrl.c_str());
    NLogError("MCP endpoint: %s", endpoint.c_str());
    if (endpoint.rfind("http://", 0) == 0) {
      NLogError("Hint: 'http://' requires a plain-HTTP router; a TLS deployment needs 'https://'.");
    }
    return 2;
  }

  if (pending.size() == 1) return RunHeadless(client, pending.front(), force, noWait, waitTimeoutSeconds);

  // The room actions above cover scripted use; the interactive screen needs a terminal.
  if (::isatty(STDIN_FILENO) == 0) {
    NLogError("ndmspc-room-tui needs an interactive terminal for the room UI.");
    NLogError("For scripted use, pick one of --list, --open <id>, --status <id>, --close <id>, "
              "--backup <file> or --restore <file>.");
    return 2;
  }

  Ndmspc::NRoomUiOptions options;
  options.serverUrl         = serverUrl;
  options.mcpEndpoint       = endpoint;
  options.refreshSeconds    = refreshSeconds;
  options.clientCert        = certFile;
  options.clientKey         = keyFile;
  options.keyPasswordFile   = keyPasswordFileForClient;
  options.caFile            = caFile;
  options.caPath            = caPath;
  options.insecure          = allowInsecure;
  options.oidc              = oidc;
  options.oidcEnabled       = oidc.Enabled();
  options.oidcTokenRefreshSeconds = oidcTokenRefresh;

  return Ndmspc::RunRoomUi(options);
}
