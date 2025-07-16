#include "NWsClient.h"
#include "NLogger.h"

#include <regex>
#include <algorithm>
#include <cstring>

// The global LWS callback function.
static int lws_callback_client_impl(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in,
                                    size_t len)
{
  Ndmspc::NLogger::Trace("LWS Callback Reason: %d", reason);
  Ndmspc::NWsClient * client = static_cast<Ndmspc::NWsClient *>(lws_wsi_user(wsi));
  // If not found in wsi user data, try to get from context
  if (!client) {
    client = (Ndmspc::NWsClient *)lws_context_user(lws_get_context(wsi));
  }

  // if (!client && reason != LWS_CALLBACK_PROTOCOL_INIT) {
  //   return 0;
  // }
  // if (!client) {
  //   Ndmspc::NLogger::Error("LWS Callback Error: NWsClient instance pointer is NULL. This is critical.");
  //   return -1;
  // }

  // Ndmspc::NLogger::Debug("LWS Callback Reason: %d", reason);
  switch (reason) {
  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    Ndmspc::NLogger::Error("Connection attempt failed, setting connected to false.");
    client->fConnected                 = false;
    client->fConnectionAttemptComplete = true;
    client->fWsi                       = nullptr;
    {
      std::lock_guard<std::mutex> lock(client->fOutgoingMutex);
      std::queue<std::string>     empty_queue;
      std::swap(client->fOutgoingMessageQueue, empty_queue);
    }
    client->fConnectCv.notify_all();
    break;

  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    Ndmspc::NLogger::Trace("LWS_CALLBACK_CLIENT_ESTABLISHED");

    client->fConnected                 = true;
    client->fWsi                       = wsi;
    client->fConnectionAttemptComplete = true;
    client->fConnectCv.notify_all();
    lws_callback_on_writable(wsi);
    break;

  case LWS_CALLBACK_CLIENT_RECEIVE: {
    // Accumulate message fragments
    static std::string messageBuffer;
    // static std::mutex  messageBufferMutex;

    {
      // std::lock_guard<std::mutex> lock(messageBufferMutex);
      messageBuffer.append(reinterpret_cast<char *>(in), len);

      // Check if this is the final fragment
      bool isFinalFragment = lws_is_final_fragment(wsi);

      if (isFinalFragment) {
        Ndmspc::NLogger::Trace("Received: %s ", messageBuffer.c_str());

        if (client->fOnMessageCallback) {
          try {
            client->fOnMessageCallback(messageBuffer);
          }
          catch (const std::exception & e) {
            Ndmspc::NLogger::Error("LWS Callback Error: User OnMessageCallback threw an exception: %s", e.what());
          }
        }
        messageBuffer.clear();
      }
    }
  } break;

  case LWS_CALLBACK_CLIENT_WRITEABLE: {
    Ndmspc::NLogger::Trace("LWS_CALLBACK_CLIENT_WRITEABLE triggered for %s", client->fHost.c_str());
    std::lock_guard<std::mutex> lock(client->fOutgoingMutex);
    if (!client->fOutgoingMessageQueue.empty()) {
      const std::string & message = client->fOutgoingMessageQueue.front();

      client->fSendBuffer.resize(LWS_PRE + message.size());
      memcpy(client->fSendBuffer.data() + LWS_PRE, message.data(), message.size());

      int n = lws_write(wsi, client->fSendBuffer.data() + LWS_PRE, message.size(), LWS_WRITE_TEXT);

      if (n < (int)message.size()) {
        Ndmspc::NLogger::Error(
            "LWS Callback Error: lws_write failed to send the full message, sent %d bytes, expected %zu bytes.", n,
            message.size());
      }
      client->fOutgoingMessageQueue.pop();

      if (!client->fOutgoingMessageQueue.empty()) {
        lws_callback_on_writable(wsi);
      }
    }

  } break;

  case LWS_CALLBACK_CLOSED:
    Ndmspc::NLogger::Trace("LWS_CALLBACK_CLOSED: Connection closed by server or client.");
    client->fConnected = false;
    client->fWsi       = nullptr;
    {
      std::lock_guard<std::mutex> lock(client->fOutgoingMutex);
      std::queue<std::string>     empty_queue;
      std::swap(client->fOutgoingMessageQueue, empty_queue);
    }
    client->fConnectionAttemptComplete = true;
    client->fConnectCv.notify_all();
    break;

  default: break;
  }
  return 0;
}

namespace Ndmspc {

// lws_protocols NWsClient::fProtocols[] = {
//     {NWsClient::fgProtocolName, lws_callback_client_impl, sizeof(Ndmspc::NWsClient *), 0}, {NULL, NULL, 0, 0}};
lws_protocols Ndmspc::NWsClient::fProtocols[] = {
    {Ndmspc::NWsClient::fgProtocolName, lws_callback_client_impl, sizeof(Ndmspc::NWsClient *), 4096},
    LWS_PROTOCOL_LIST_TERM /* terminator */
};
NWsClient::NWsClient(int maxRetries, int retryDelayMs)
    : fLwsContext(nullptr), fWsi(nullptr), fConnected(false), fShutdownRequested(false),
      fConnectionAttemptComplete(false), fMaxRetries(maxRetries), fRetryDelayMs(retryDelayMs)
{
  // Silent websocket logging
  lws_set_log_level(LLL_ERR, NULL);
  // lws_set_log_level(LLL_DEBUG | LLL_PARSER | LLL_HEADER, NULL);
}

NWsClient::~NWsClient()
{
  Disconnect();
  NLogger::Debug("NWsClient destructor finished.");
}

bool NWsClient::Connect(const std::string & uriString)
{
  int attempt = 0;
  while (attempt < fMaxRetries) {
    NLogger::Info("NWsClient: Attempting to connect to %s (attempt %d)", uriString.c_str(), attempt + 1);
    if (fConnected.load()) {
      NLogger::Error("NWsClient: Already connected.");
      return true;
    }
    if (fLwsContext) {
      NLogger::Error("NWsClient: Context already exists, disconnect first.");
      return false;
    }

    fShutdownRequested         = false;
    fConnectionAttemptComplete = false;
    fWsi                       = nullptr;

    WS_URI parsedUri;
    try {
      parsedUri = ParseUri(uriString);
    }
    catch (const std::runtime_error & e) {
      NLogger::Error("NWsClient: URI parsing error: %s", e.what());
      return false;
    }

    fHost = parsedUri.fHost;
    fPort = parsedUri.fPort;
    fPath = parsedUri.fPath;

    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = fProtocols;
    info.gid       = -1;
    info.uid       = -1;

    if (parsedUri.fScheme == "wss") {
      info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }
    info.user = this;

    fLwsContext = lws_create_context(&info);
    if (!fLwsContext) {
      NLogger::Error("Failed to create libwebsockets context.");
      return false;
    }

    struct lws_client_connect_info cci;
    memset(&cci, 0, sizeof(cci));
    cci.context = fLwsContext;
    cci.address = fHost.c_str();
    cci.port    = fPort;
    cci.path    = fPath.c_str();
    cci.host    = fHost.c_str();
    cci.origin  = fHost.c_str();

    cci.protocol = fgProtocolName;
    cci.userdata = this;

    if (parsedUri.fScheme == "wss") {
      cci.ssl_connection = LCCSCF_USE_SSL;
    }
    else {
      cci.ssl_connection = 0;
    }

    NLogger::Trace("NWsClient: Initiating connection to %s (attempt %d)", uriString.c_str(), attempt + 1);

    fWsi = lws_client_connect_via_info(&cci);
    if (!fWsi) {
      NLogger::Error("NWsClient: Failed to connect to WebSocket server at %s (attempt %d)", uriString.c_str(),
                     attempt + 1);
      lws_context_destroy(fLwsContext);
      fLwsContext = nullptr;
      attempt++;
      if (attempt < fMaxRetries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(fRetryDelayMs));
        continue;
      }
      return false;
    }
    cci.pwsi = &fWsi;

    fLwsServiceThread = std::thread(&NWsClient::LwsServiceLoop, this);

    {
      std::unique_lock<std::mutex> lock(fConnectMutex);
      fConnectCv.wait(lock, [this]() { return fConnectionAttemptComplete.load() || fShutdownRequested.load(); });
    }
    if (!fConnected.load()) {
      NLogger::Error("NWsClient: Connection to %s failed or shutdown requested (attempt %d).", uriString.c_str(),
                     attempt + 1);
      Disconnect();
      attempt++;
      if (attempt < fMaxRetries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(fRetryDelayMs));
        continue;
      }
      return false;
    }

    return true;
  }
  return false;
}

void NWsClient::Disconnect()
{
  NLogger::Trace("NWsClient: Disconnect requested.");
  if (fLwsContext) {
    fShutdownRequested         = true;
    fConnectionAttemptComplete = true;
    fConnectCv.notify_all();

    lws_cancel_service(fLwsContext);

    if (fLwsServiceThread.joinable()) {
      fLwsServiceThread.join();
    }

    if (fWsi) {
      fWsi = nullptr;
    }

    lws_context_destroy(fLwsContext);
    fLwsContext = nullptr;
    fConnected  = false;

    std::queue<std::string>     empty_queue;
    std::lock_guard<std::mutex> lock(fOutgoingMutex);
    std::swap(fOutgoingMessageQueue, empty_queue);

    NLogger::Trace("NWsClient: LWS context destroyed.");
  }
}

bool NWsClient::Send(const std::string & message)
{
  if (!fConnected.load() || !fWsi) {
    NLogger::Error("NWsClient: Cannot send, not connected to WebSocket server.");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(fOutgoingMutex);
    fOutgoingMessageQueue.push(message);
  }

  lws_callback_on_writable(fWsi);
  NLogger::Trace("NWsClient: Called lws_callback_on_writable for message. Queue size: %lld",
                 fOutgoingMessageQueue.size()); // <-- ADD THIS

  return true;
}

void NWsClient::LwsServiceLoop()
{
  NLogger::Trace("NWsClient: LWS service loop started.");
  int n = 0;
  while (!fShutdownRequested.load()) {
    n = lws_service(fLwsContext, -1);
    // NLogger::Debug("NWsClient: lws_service returned %d", n);
    if (n < 0) {
      NLogger::Error("NWsClient: lws_service returned error or was cancelled, stopping loop.");
      fShutdownRequested         = true;
      fConnected                 = false;
      fConnectionAttemptComplete = true;
      fConnectCv.notify_all();
      break;
    }
    // sleep for a short duration to avoid busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  NLogger::Trace("NWsClient: LWS service loop stopped.");
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
