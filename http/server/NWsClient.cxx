#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "ndmspc/core/NLogger.h"
#include "NWsClient.h"

namespace {
constexpr int kConnectTimeoutSec = 30; ///< TCP/TLS connect timeout
constexpr int kPingIntervalSec   = 30; ///< Keepalive ping interval
constexpr int kAuthTimeoutSec    = 15; ///< Budget for the OIDC authenticate handshake
// A read timeout is fatal to the httplib WebSocket (any failed read closes it), so once
// connected it must stay comfortably above the ping interval to survive idle periods.
constexpr int kReadTimeoutSec = 300;

// OpenSSL may write() to a socket the peer has already closed, e.g. when the server
// rejects the TLS handshake (missing/invalid client certificate). That raises SIGPIPE,
// which by default terminates the process; ignoring it lets connect() report a
// connection error instead of aborting.
void IgnoreSigpipeOnce()
{
  static std::once_flag once;
  std::call_once(once, []() { std::signal(SIGPIPE, SIG_IGN); });
}

// Reads a whole file into memory. httplib's WebSocket client only accepts a client
// certificate through its in-memory PemMemory constructor (no file-path overload).
std::string ReadFile(const std::string & path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) throw std::runtime_error("NWsClient: cannot open '" + path + "'");
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}
} // namespace

namespace Ndmspc {

/**
 * @brief Implementation state of the WebSocket client.
 *
 * Keeps the httplib WebSocket client and its TLS material out of the public header.
 */
struct NWsClient::Impl {
  /**
   * @brief Constructor.
   * @param maxRetries Maximum number of connection retries.
   * @param retryDelayMs Delay between retries in milliseconds.
   */
  explicit Impl(int maxRetries, int retryDelayMs) : fMaxRetries(maxRetries), fRetryDelayMs(retryDelayMs) {}

  std::unique_ptr<httplib::ws::WebSocketClient> fClient; ///< Underlying httplib WebSocket client
  std::thread                                   fReaderThread; ///< Background reader thread
  std::atomic<bool>                             fConnected{false}; ///< Whether the handshake completed
  std::atomic<bool>                             fShutdownRequested{false}; ///< Ask the reader thread to stop
  std::atomic<bool>                             fAuthenticated{false}; ///< Whether the server acknowledged auth

  int         fMaxRetries;   ///< Maximum number of connection retries
  int         fRetryDelayMs; ///< Delay between retries in milliseconds
  std::string fAuthToken;    ///< OAuth2/OIDC access token presented on connect

  NWsClient::OnMessageCallback fOnMessageCallback; ///< User callback for received messages

  // TLS / X509 client authentication configuration (set before Connect()).
  std::string fClientCertFile; ///< PEM certificate presented to the server
  std::string fClientKeyFile;  ///< PEM private key for fClientCertFile
  std::string fClientKeyPassword; ///< Passphrase for fClientKeyFile (may be empty)
  std::string fCaFile;         ///< CA bundle used to verify the server certificate
  std::string fCaPath;         ///< Hashed CA directory used to verify the server certificate
  std::string fCertPem; ///< PEM contents kept alive for the client's PemMemory
  std::string fKeyPem;  ///< PEM contents kept alive for the client's PemMemory
  bool        fVerifyServer{true};      ///< Whether to verify the server certificate chain
  bool        fAllowSelfSigned{false};  ///< Whether to tolerate missing/self-signed server CAs
  bool        fSkipHostnameCheck{false}; ///< Whether to skip hostname verification
};

NWsClient::NWsClient(int maxRetries, int retryDelayMs) : fImpl(std::make_unique<Impl>(maxRetries, retryDelayMs)) {}

NWsClient::~NWsClient()
{
  Disconnect();
  NLogDebug("NWsClient destructor finished.");
}

void NWsClient::SetAuthenticationToken(const std::string & token)
{
  fImpl->fAuthToken = token;
}

void NWsClient::SetClientCertificate(const std::string & certFilePath, const std::string & keyFilePath,
                                     const std::string & keyPassword)
{
  fImpl->fClientCertFile = certFilePath;
  fImpl->fClientKeyFile = keyFilePath;
  fImpl->fClientKeyPassword = keyPassword;
}

void NWsClient::SetCaFile(const std::string & caFilePath)
{
  fImpl->fCaFile = caFilePath;
}

void NWsClient::SetCaPath(const std::string & caDirPath)
{
  fImpl->fCaPath = caDirPath;
}

void NWsClient::SetServerVerification(bool verifyServer, bool allowSelfSigned, bool skipHostnameCheck)
{
  fImpl->fVerifyServer = verifyServer;
  fImpl->fAllowSelfSigned = allowSelfSigned;
  fImpl->fSkipHostnameCheck = skipHostnameCheck;
}

void NWsClient::SetOnMessageCallback(OnMessageCallback callback)
{
  fImpl->fOnMessageCallback = std::move(callback);
}

bool NWsClient::IsConnected() const
{
  return fImpl->fConnected.load();
}

bool NWsClient::IsAuthenticated() const
{
  return fImpl->fAuthenticated.load();
}

bool NWsClient::CreateClient(const std::string & url, bool isSecure)
{
  httplib::Headers headers;
  headers.emplace("Sec-WebSocket-Protocol", fgProtocolName);

  const bool useCert = !fImpl->fClientCertFile.empty() && !fImpl->fClientKeyFile.empty();
  if (isSecure && useCert) {
    try {
      fImpl->fCertPem = ReadFile(fImpl->fClientCertFile);
      fImpl->fKeyPem = ReadFile(fImpl->fClientKeyFile);
    }
    catch (const std::exception & e) {
      NLogError("%s", e.what());
      return false;
    }
    httplib::ws::WebSocketClient::PemMemory pem{fImpl->fCertPem.c_str(), fImpl->fCertPem.size(),
                                                fImpl->fKeyPem.c_str(),  fImpl->fKeyPem.size(),
                                                fImpl->fClientKeyPassword.c_str()};
    fImpl->fClient = std::make_unique<httplib::ws::WebSocketClient>(url, pem, headers);
  }
  else {
    fImpl->fClient = std::make_unique<httplib::ws::WebSocketClient>(url, headers);
  }

  if (!fImpl->fClient->is_valid()) {
    NLogError("NWsClient: invalid WebSocket URL or TLS setup for '%s'", url.c_str());
    if (isSecure && useCert) {
      NLogError("NWsClient: could not load the client certificate/key from '%s' and '%s'; "
                "if the private key is encrypted, provide its passphrase.",
                fImpl->fClientCertFile.c_str(), fImpl->fClientKeyFile.c_str());
    }
    fImpl->fClient.reset();
    return false;
  }

  if (!fImpl->fCaFile.empty() || !fImpl->fCaPath.empty())
    fImpl->fClient->set_ca_cert_path(fImpl->fCaFile, fImpl->fCaPath);
  // httplib exposes a single chain-verification switch (plus hostname verification), so
  // allowing self-signed certificates maps to disabling chain verification.
  fImpl->fClient->enable_server_certificate_verification(fImpl->fVerifyServer && !fImpl->fAllowSelfSigned);
  fImpl->fClient->enable_server_hostname_verification(fImpl->fVerifyServer && !fImpl->fSkipHostnameCheck);
  fImpl->fClient->set_connection_timeout(kConnectTimeoutSec);
  fImpl->fClient->set_websocket_ping_interval(kPingIntervalSec);
  return true;
}

bool NWsClient::Connect(const std::string & uriString)
{
  IgnoreSigpipeOnce();

  WS_URI parsedUri;
  try {
    parsedUri = ParseUri(uriString);
  }
  catch (const std::runtime_error & e) {
    NLogError("NWsClient: URI parsing error: %s", e.what());
    return false;
  }

  const bool        isSecure = parsedUri.fScheme == "wss";
  const std::string url = parsedUri.fScheme + "://" + parsedUri.fHost + ":" + std::to_string(parsedUri.fPort) +
                          parsedUri.fPath;

  int attempt = 0;
  while (attempt < fImpl->fMaxRetries) {
    NLogInfo("NWsClient: Attempting to connect to %s (attempt %d)", uriString.c_str(), attempt + 1);
    if (fImpl->fConnected.load()) {
      NLogError("NWsClient: Already connected.");
      return true;
    }
    if (fImpl->fClient) {
      NLogError("NWsClient: Client already exists, disconnect first.");
      return false;
    }

    fImpl->fShutdownRequested = false;
    fImpl->fAuthenticated = false;

    bool ready = false;
    if (!CreateClient(url, isSecure)) {
      // An invalid URL or an unloadable client certificate/key (for example a wrong
      // passphrase) is a configuration error: it is identical on every attempt, so
      // retrying cannot succeed. Fail immediately instead of looping.
      return false;
    }

    auto result = fImpl->fClient->connect();
    if (result) {
      fImpl->fConnected = true;
      NLogDebug("NWsClient: WebSocket handshake completed with %s", url.c_str());

      if (fImpl->fAuthToken.empty()) {
        ready = true;
      }
      else {
        fImpl->fClient->set_read_timeout(kAuthTimeoutSec, 0);
        if (PerformAuthHandshake()) {
          ready = true;
        }
        else {
          NLogError("NWsClient: authentication was not completed for %s (attempt %d).", uriString.c_str(),
                    attempt + 1);
        }
      }

      if (ready) {
        fImpl->fClient->set_read_timeout(kReadTimeoutSec, 0);
        fImpl->fReaderThread = std::thread(&NWsClient::ReaderLoop, this);
        return true;
      }
    }
    else {
      NLogError("NWsClient: Failed to connect to %s: %s", url.c_str(),
                httplib::to_string(result.error()).c_str());
    }

    // Clean up the failed attempt and retry.
    Disconnect();
    attempt++;
    if (attempt >= fImpl->fMaxRetries) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(fImpl->fRetryDelayMs));
  }
  return false;
}

bool NWsClient::PerformAuthHandshake()
{
  const std::string frame = "{\"event\":\"authenticate\",\"token\":\"" + fImpl->fAuthToken + "\"}";
  if (!fImpl->fClient->send(frame)) {
    NLogError("NWsClient: failed to send the authenticate frame.");
    return false;
  }

  std::string message;
  while (!fImpl->fShutdownRequested.load()) {
    if (fImpl->fClient->read(message) == httplib::ws::ReadResult::Fail) {
      NLogError("NWsClient: connection closed while waiting for the authentication acknowledgement.");
      return false;
    }
    HandleIncoming(message);
    if (fImpl->fAuthenticated.load()) return true;
  }
  return false;
}

void NWsClient::HandleIncoming(const std::string & message)
{
  // Detect the server's OIDC "authenticated" acknowledgement.
  try {
    nlohmann::json parsed = nlohmann::json::parse(message);
    if (parsed.is_object() && parsed.value("event", "") == "authenticated") {
      fImpl->fAuthenticated = true;
    }
  }
  catch (const std::exception &) {
    // Not JSON - still forward the raw payload to the user callback.
  }

  if (fImpl->fOnMessageCallback) {
    try {
      fImpl->fOnMessageCallback(message);
    }
    catch (const std::exception & e) {
      NLogError("NWsClient: user OnMessageCallback threw an exception: %s", e.what());
    }
  }
}

void NWsClient::ReaderLoop()
{
  NLogTrace("NWsClient: reader loop started.");
  std::string message;
  while (!fImpl->fShutdownRequested.load()) {
    if (fImpl->fClient->read(message) == httplib::ws::ReadResult::Fail) break;
    HandleIncoming(message);
  }
  fImpl->fConnected = false;
  NLogTrace("NWsClient: reader loop stopped.");
}

void NWsClient::Disconnect()
{
  if (!fImpl->fClient) {
    fImpl->fConnected = false;
    return;
  }

  NLogTrace("NWsClient: Disconnect requested.");
  fImpl->fShutdownRequested = true;

  // Send a Close frame; a reader blocked in read() is released once the peer closes
  // the connection (or its read timeout expires).
  fImpl->fClient->close(httplib::ws::CloseStatus::Normal, "");
  if (fImpl->fReaderThread.joinable()) fImpl->fReaderThread.join();
  fImpl->fClient.reset();

  fImpl->fConnected = false;
  fImpl->fAuthenticated = false;
  NLogTrace("NWsClient: disconnected.");
}

bool NWsClient::Send(const std::string & message)
{
  if (!fImpl->fConnected.load() || !fImpl->fClient) {
    NLogError("NWsClient: Cannot send, not connected to WebSocket server.");
    return false;
  }

  try {
    if (!fImpl->fClient->send(message)) {
      NLogError("NWsClient: failed to send message over the WebSocket.");
      return false;
    }
  }
  catch (const std::exception & e) {
    NLogError("NWsClient: exception while sending a message: %s", e.what());
    return false;
  }
  return fImpl->fConnected.load();
}

WS_URI NWsClient::ParseUri(const std::string & uriString)
{
  WS_URI parsed;
  parsed.fPort = 0;

  std::regex  uriRegex(R"((ws|wss)://([a-zA-Z0-9\-\.]+)(:([0-9]+))?(/.*)?)");
  std::smatch matches;

  if (!std::regex_match(uriString, matches, uriRegex)) {
    throw std::runtime_error("Invalid WebSocket URI format: " + uriString);
  }

  parsed.fScheme = matches[1].str();
  std::transform(parsed.fScheme.begin(), parsed.fScheme.end(), parsed.fScheme.begin(), ::tolower);

  parsed.fHost = matches[2].str();

  if (matches[4].matched) {
    try {
      parsed.fPort = std::stoi(matches[4].str());
    }
    catch (const std::exception & e) {
      throw std::runtime_error("Invalid port number in URI: " + matches[4].str());
    }
  }
  else {
    if (parsed.fScheme == "ws") {
      parsed.fPort = 80;
    }
    else if (parsed.fScheme == "wss") {
      parsed.fPort = 443;
    }
    else {
      throw std::runtime_error("Unknown scheme for default port: " + parsed.fScheme);
    }
  }

  parsed.fPath = matches[5].str();
  if (parsed.fPath.empty()) {
    parsed.fPath = "/";
  }

  return parsed;
}

} // namespace Ndmspc
