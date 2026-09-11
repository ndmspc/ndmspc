#include <CLI/CLI.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include <termios.h>
#include <unistd.h>

#include <TROOT.h>
#include <TApplication.h>
#include <TBase64.h>
#include <TSystem.h>
#include <TString.h>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/http/NOidcTokenClient.h"
#include "ndmspc/http/NWsClient.h"

namespace {

// Reads a line from the terminal with echo disabled, for passphrase entry.
std::string ReadPassphrase(const std::string & prompt)
{
  std::fputs(prompt.c_str(), stderr);
  std::fflush(stderr);

  termios original{};
  if (::tcgetattr(STDIN_FILENO, &original) != 0) {
    std::fputs("\n", stderr);
    return {};
  }
  termios hidden = original;
  hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
  ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);

  std::string passphrase;
  char        ch = 0;
  while (std::fread(&ch, 1, 1, stdin) == 1) {
    if (ch == '\n' || ch == '\r') break;
    passphrase.push_back(ch);
  }

  ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
  std::fputs("\n", stderr);
  return passphrase;
}

// Detects whether a PEM private key is passphrase-protected (PKCS#8 or legacy PEM).
bool IsKeyEncrypted(const std::string & keyFile)
{
  std::ifstream in(keyFile, std::ios::binary);
  if (!in.is_open()) return false;
  const std::string contents{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  return contents.find("ENCRYPTED PRIVATE KEY") != std::string::npos ||
         contents.find("Proc-Type: 4,ENCRYPTED") != std::string::npos ||
         contents.find("DEK-Info:") != std::string::npos;
}

// Decodes a base64-encoded passphrase file (same convention as ~/.globus/password.txt).
bool ReadPassphraseFile(const std::string & path, std::string & out)
{
  std::ifstream file(path);
  if (!file.is_open()) {
    NLogError("Cannot open the key passphrase file: %s", path.c_str());
    return false;
  }
  std::string encoded;
  std::getline(file, encoded);
  out = TBase64::Decode(TString(encoded)).Data();
  if (!out.empty() && out.back() == '\r') out.pop_back();
  return true;
}

// Resolves the private-key passphrase from, in order: an explicit value (--key-pass /
// NDMSPC_KEY_PASS), a base64 passphrase file, or an interactive prompt when the key is
// encrypted and a terminal is available. A non-interactive run with an encrypted key and
// no passphrase source fails with an actionable error instead of blocking.
bool ResolveKeyPassphrase(const std::string & keyFile, const std::string & inlinePass,
                          const std::string & passFile, std::string & out)
{
  if (!inlinePass.empty()) {
    out = inlinePass;
    return true;
  }
  if (!passFile.empty()) return ReadPassphraseFile(passFile, out);
  if (keyFile.empty() || !IsKeyEncrypted(keyFile)) return true;

  if (::isatty(STDIN_FILENO)) {
    out = ReadPassphrase("Enter passphrase for key '" + keyFile + "': ");
    if (out.empty()) {
      NLogError("No passphrase supplied for the encrypted key '%s'", keyFile.c_str());
      return false;
    }
    return true;
  }

  NLogError("The private key '%s' is encrypted but no passphrase was provided in a "
            "non-interactive session.",
            keyFile.c_str());
  NLogError("Provide one with --key-pass, --key-pass-file <base64 file>, or the "
            "NDMSPC_KEY_PASS environment variable.");
  return false;
}

} // namespace

void handle_sigterm(int /*sig*/)
{
  gSystem->Exit(0);
}

std::string app_description()
{
  return "ndmspc-ws-client - WebSocket client with optional X509 (mTLS) client-certificate support";
}

// Logs actionable hints for the most common connection mistakes: a plain ws:// scheme
// against a TLS server, or a URL without the WebSocket endpoint path.
void LogConnectionHints(const std::string & url)
{
  Ndmspc::WS_URI parsed;
  try {
    parsed = Ndmspc::NWsClient::ParseUri(url);
  } catch (const std::exception &) {
    return;
  }
  if (parsed.fScheme == "ws") {
    NLogError("Hint: the URL uses 'ws://' (unencrypted); a TLS/mTLS server requires 'wss://'.");
  }
  if (parsed.fPath.empty() || parsed.fPath == "/") {
    NLogError("Hint: the URL has no WebSocket path; the NDMSPC endpoint is '/ws/root.websocket'.");
  }
}

int main(int argc, char ** argv)
{
  signal(SIGTERM, handle_sigterm);

  gROOT->SetBatch(kTRUE);
  TApplication rootApp("ndmspc-ws-client", 0, nullptr);

  std::string url        = "ws://localhost:8080/ws/root.websocket";
  std::string certFile;
  std::string keyFile;
  std::string keyPassword;
  std::string keyPasswordFile;
  std::string caFile;
  std::string caPath;
  bool        allowInsecure = false;
  bool        allowSelfSigned = false;
  bool        skipHostname  = false;
  int         maxRetries    = 5;
  int         retryDelayMs  = 1000;
  int         timeoutSec    = 0;

  // OAuth2/Keycloak options for WebSocket authentication.
  std::string oidcIssuer;
  std::string oidcClientId;
  std::string oidcClientSecret;
  std::string oidcUsername;
  std::string oidcPassword;
  std::string oidcGrant     = "client_credentials";
  std::string oidcCaFile;
  std::string oidcCaPath;
  bool        oidcAllowInsecureHttp = false;

  CLI::App app{app_description()};
  app.set_version_flag("--version", "ndmspc-ws-client", "Print version information and exit");
  app.set_help_all_flag("--help-all", "Expand all help");

  app.add_option("--url,-u", url, "WebSocket URL to connect to (default: ws://localhost:8080/ws/root.websocket)");
  app.add_option("--cert", certFile, "Client certificate (PEM) for mutual TLS (X509)");
  app.add_option("--key", keyFile, "Client private key (PEM) for mutual TLS (X509)");
  app.add_option("--key-pass", keyPassword, "Password for the client private key")
      ->envname("NDMSPC_KEY_PASS");
  app.add_option("--key-pass-file", keyPasswordFile,
                 "Base64-encoded file holding the private-key password (as ~/.globus/password.txt)")
      ->envname("NDMSPC_KEY_PASS_FILE");
  app.add_option("--ca-file", caFile, "CA bundle used to verify the server certificate");
  app.add_option("--ca-path", caPath, "CA directory (hashed certificates) used to verify the server certificate");
  app.add_flag("--allow-insecure", allowInsecure, "Do not verify the server certificate (use for self-signed test servers)");
  app.add_flag("--allow-self-signed", allowSelfSigned, "Allow self-signed or untrusted server certificates");
  app.add_flag("--skip-hostname-check", skipHostname, "Skip server hostname verification");
  app.add_option("-r,--retries", maxRetries, "Maximum connection retries (default: 5)");
  app.add_option("--retry-delay", retryDelayMs, "Delay between retries in ms (default: 1000)");
  app.add_option("-t,--timeout", timeoutSec, "Exit after this many seconds (0 = run until interrupted)");

  // OAuth2/Keycloak options to obtain an access token for WS authentication.
  app.add_option("--oidc-issuer", oidcIssuer, "OIDC issuer URL, e.g. http://localhost:8081/realms/ndmspc");
  app.add_option("--oidc-client-id", oidcClientId, "OIDC client identifier");
  app.add_option("--oidc-client-secret", oidcClientSecret, "OIDC client secret (optional for public clients)");
  app.add_option("--oidc-grant", oidcGrant, "OAuth2 grant: 'client_credentials' (default) or 'password'")
      ->check(CLI::IsMember({"client_credentials", "password"}));
  app.add_option("--oidc-username", oidcUsername, "Resource owner username (password grant only)");
  app.add_option("--oidc-password", oidcPassword, "Resource owner password (password grant only)");
  app.add_option("--oidc-ca-file", oidcCaFile, "CA bundle used to verify the OIDC issuer's TLS certificate");
  app.add_option("--oidc-ca-path", oidcCaPath, "CA directory (hashed certificates) used to verify the OIDC issuer's TLS certificate");
  app.add_flag("--oidc-allow-insecure-http", oidcAllowInsecureHttp, "Allow http:// OIDC issuer (development only)");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError & e) {
    return app.exit(e);
  }

  Ndmspc::NWsClient client(maxRetries, retryDelayMs);

  // Configure X509 client-certificate (mTLS) support.
  if (!certFile.empty() || !keyFile.empty()) {
    std::string keyPassphrase;
    if (!ResolveKeyPassphrase(keyFile, keyPassword, keyPasswordFile, keyPassphrase)) return 2;
    client.SetClientCertificate(certFile, keyFile, keyPassphrase);
    NLogInfo("X509 client certificate enabled (cert=%s key=%s)", certFile.c_str(), keyFile.c_str());
  }
  if (!caFile.empty()) {
    client.SetCaFile(caFile);
    NLogInfo("Using CA bundle %s to verify the server certificate", caFile.c_str());
  }
  if (!caPath.empty()) {
    client.SetCaPath(caPath);
    NLogInfo("Using CA directory %s to verify the server certificate", caPath.c_str());
  }
  client.SetServerVerification(!allowInsecure, allowSelfSigned, skipHostname);

  // OAuth2/Keycloak: obtain an access token and use it for the server's WebSocket
  // authenticate frame.
  if (!oidcIssuer.empty() && !oidcClientId.empty()) {
    Ndmspc::NOidcTokenClientConfig tokenCfg;
    tokenCfg.issuer = oidcIssuer;
    tokenCfg.clientId = oidcClientId;
    tokenCfg.clientSecret = oidcClientSecret;
    tokenCfg.username = oidcUsername;
    tokenCfg.password = oidcPassword;
    tokenCfg.grant = oidcGrant == "password" ? Ndmspc::NOidcTokenClientConfig::Grant::Password
                                            : Ndmspc::NOidcTokenClientConfig::Grant::ClientCredentials;
    tokenCfg.caFile = oidcCaFile;
    tokenCfg.caPath = oidcCaPath;
    tokenCfg.allowInsecureHttp = oidcAllowInsecureHttp;

    try {
      Ndmspc::NOidcTokenClient tokenClient(tokenCfg);
      const std::string token = tokenClient.ObtainAccessToken();
      client.SetAuthenticationToken(token);
      NLogInfo("Obtained OIDC access token (%zu bytes) for WebSocket authentication", token.size());
    } catch (const std::exception & e) {
      NLogError("Failed to obtain an OIDC access token: %s", e.what());
      return 2;
    }
  } else if (!oidcIssuer.empty() || !oidcClientId.empty()) {
    NLogError("OIDC authentication requires both --oidc-issuer and --oidc-client-id");
    return 2;
  }

  client.SetOnMessageCallback([](const std::string & message) {
    NLogInfo("RECEIVED: %s", message.c_str());
  });

  if (!client.Connect(url)) {
    NLogError("Failed to connect to %s", url.c_str());
    LogConnectionHints(url);
    return 1;
  }
  NLogInfo("Connected to %s", url.c_str());
  if (client.IsAuthenticated()) {
    NLogInfo("WebSocket authenticated via OAuth2/OIDC");
  }

  // Run until a timeout expires or the process is interrupted.
  if (timeoutSec > 0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
    while (client.IsConnected() && std::chrono::steady_clock::now() < deadline) {
      gSystem->ProcessEvents();
      gSystem->Sleep(100);
    }
  } else {
    while (client.IsConnected()) {
      gSystem->ProcessEvents();
      gSystem->Sleep(100);
    }
  }

  client.Disconnect();
  NLogInfo("Disconnected cleanly");
  return 0;
}