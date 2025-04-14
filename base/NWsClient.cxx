#include <iostream>
#include <string>
#include <chrono>
#include <cstring>
#include "NWsClient.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NWsClient);
/// \endcond

namespace Ndmspc {

// Define the protocols array
struct lws_protocols NWsClient::protocols_[] = {
    {"ws-protocol", NWsClient::callback_function, 0, 8192, 0, nullptr, 0},
    {nullptr, nullptr, 0, 0, 0, nullptr, 0} // terminator
};
NWsClient::NWsClient() : context_(nullptr), wsi_(nullptr), connected_(false), thread_running_(false), pending_tx_(false)
{
  ///
  /// Constructor
  ///

  // Silent websocket logging
  lws_set_log_level(LLL_ERR, NULL);
}

NWsClient::~NWsClient()
{
  ///
  /// Destructor
  ///

  disconnect();
}

bool NWsClient::connect(const std::string & url)
{
  ///
  /// Connect to the WebSocket server
  ///

  // Parse URL
  std::string protocol, address, path;
  int         port;
  bool        use_ssl = false;

  if (url.substr(0, 5) == "ws://") {
    protocol = "ws";
    address  = url.substr(5);
    use_ssl  = false;
  }
  else if (url.substr(0, 6) == "wss://") {
    protocol = "wss";
    address  = url.substr(6);
    use_ssl  = true;
  }
  else {
    std::cerr << "Invalid URL scheme. Use ws:// or wss://" << std::endl;
    return false;
  }

  // Extract host, port, and path
  size_t path_pos = address.find('/');
  if (path_pos != std::string::npos) {
    path    = address.substr(path_pos);
    address = address.substr(0, path_pos);
  }
  else {
    path = "/";
  }

  size_t port_pos = address.find(':');
  if (port_pos != std::string::npos) {
    port    = std::stoi(address.substr(port_pos + 1));
    address = address.substr(0, port_pos);
  }
  else {
    port = use_ssl ? 443 : 80;
  }

  // Store for later use
  host_ = address;

  // Create libwebsocket context
  struct lws_context_creation_info info;
  memset(&info, 0, sizeof(info));

  info.port      = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols_;
  info.gid       = -1;
  info.uid       = -1;
  info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.user      = this; // Store this pointer for later retrieval

  context_ = lws_create_context(&info);
  if (!context_) {
    std::cerr << "Failed to create LWS context" << std::endl;
    return false;
  }

  // Set up connection info
  struct lws_client_connect_info conn_info;
  memset(&conn_info, 0, sizeof(conn_info));

  conn_info.context  = context_;
  conn_info.address  = address.c_str();
  conn_info.port     = port;
  conn_info.path     = path.c_str();
  conn_info.host     = address.c_str();
  conn_info.origin   = address.c_str();
  conn_info.protocol = protocols_[0].name;
  conn_info.userdata = this; // Pass the client instance as user data

  if (use_ssl) {
    conn_info.ssl_connection = LCCSCF_USE_SSL;
  }

  // Connect
  wsi_ = lws_client_connect_via_info(&conn_info);
  if (!wsi_) {
    std::cerr << "Failed to connect" << std::endl;
    lws_context_destroy(context_);
    context_ = nullptr;
    return false;
  }

  // Start service thread
  thread_running_ = true;
  service_thread_ = std::thread([this]() {
    while (thread_running_ && context_) {
      lws_service(context_, 50);
    }
  });

  // Wait for connection to establish or fail
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return connected_ || !thread_running_; });

  return connected_;
}

void NWsClient::disconnect()
{
  ///
  /// Disconnect from the WebSocket server
  ///

  thread_running_ = false;

  if (service_thread_.joinable()) {
    service_thread_.join();
  }

  if (context_) {
    lws_context_destroy(context_);
    context_ = nullptr;
  }

  wsi_       = nullptr;
  connected_ = false;
}

bool NWsClient::send(const std::string & message)
{
  if (!connected_ || !wsi_) {
    std::cerr << "Not connected" << std::endl;
    return false;
  }

  // Prepare message with LWS_PRE padding
  send_buffer_.resize(LWS_PRE + message.size());
  memcpy(send_buffer_.data() + LWS_PRE, message.data(), message.size());

  pending_tx_ = true;

  // Request a callback for writable
  lws_callback_on_writable(wsi_);

  // Wait for the message to be sent
  std::unique_lock<std::mutex> lock(mutex_);
  bool result = cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return !pending_tx_ || !connected_; });

  return result && connected_;
}

std::string NWsClient::receive(int timeout_ms)
{
  ///
  /// Receive a message from the WebSocket server
  ///

  std::unique_lock<std::mutex> lock(mutex_);

  bool success = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [this]() { return !received_messages_.empty() || !connected_; });

  if (!success || !connected_ || received_messages_.empty()) {
    return "";
  }

  std::string message = received_messages_.front();
  received_messages_.erase(received_messages_.begin());
  return message;
}

bool NWsClient::is_connected() const
{
  ///
  /// Check if the client is connected
  ///

  return connected_;
}

int NWsClient::callback_function(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in, size_t len)
{
  ///
  /// WebSocket callback function
  ///

  // Get client instance from user data
  NWsClient * client = nullptr;

  // Try to get the client instance from the per-connection user data
  client = (NWsClient *)lws_wsi_user(wsi);

  // If not found in wsi user data, try to get from context
  if (!client) {
    client = (NWsClient *)lws_context_user(lws_get_context(wsi));
  }

  if (!client && reason != LWS_CALLBACK_PROTOCOL_INIT) {
    return 0;
  }

  switch (reason) {
  case LWS_CALLBACK_PROTOCOL_INIT:
    // Nothing to do here since we can't modify the protocol's user field
    break;

  case LWS_CALLBACK_CLIENT_ESTABLISHED: {
    if (client) {
      std::lock_guard<std::mutex> lock(client->mutex_);
      client->connected_ = true;
      client->cv_.notify_all();
      std::cout << "Connected to server" << std::endl;
    }
  } break;

  case LWS_CALLBACK_CLIENT_RECEIVE: {
    if (client) {

      bool is_first = lws_is_first_fragment(wsi);
      bool is_final = lws_is_final_fragment(wsi);

      unsigned char * payload = (unsigned char *)in;
      // bool is_first = lws_rx_flag_first(wsi);
      // bool is_final = lws_rx_flag_final(wsi);
      // // bool is_text = lws_rx_flag_text(wsi); // Assuming text for simplicity.
      //
      if (is_first) {
        client->rx_buffer_.clear();
      }

      client->rx_buffer_.insert(client->rx_buffer_.end(), payload, payload + len);

      if (is_final) {
        std::lock_guard<std::mutex> lock(client->mutex_);
        std::string                 received_message(client->rx_buffer_.begin(), client->rx_buffer_.end());
        client->received_messages_.push_back(received_message);
        client->rx_buffer_.clear();
        client->cv_.notify_one(); // Notify receive()
        // client->cv_.notify_all();
      }

      // // Handle received data
      // std::cout << "Received message: len:" << len << std::endl;
      // std::string                 message(static_cast<char *>(in), len);
      // std::lock_guard<std::mutex> lock(client->mutex_);
      // client->received_messages_.push_back(message);
      // client->cv_.notify_all();
    }
  } break;

  case LWS_CALLBACK_CLIENT_WRITEABLE: {
    if (client && client->pending_tx_) {
      // Write pending message
      int bytes_sent =
          lws_write(wsi, client->send_buffer_.data() + LWS_PRE, client->send_buffer_.size() - LWS_PRE, LWS_WRITE_TEXT);

      std::lock_guard<std::mutex> lock(client->mutex_);
      client->pending_tx_ = false;
      client->cv_.notify_all();

      if (bytes_sent < 0) {
        std::cerr << "Error writing to socket" << std::endl;
        return -1;
      }
    }
  } break;

  case LWS_CALLBACK_CLIENT_CLOSED:
  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
    if (client) {
      std::string reason_str = (reason == LWS_CALLBACK_CLIENT_CLOSED) ? "Connection closed" : "Connection error";
      std::cout << reason_str << std::endl;

      std::lock_guard<std::mutex> lock(client->mutex_);
      client->connected_ = false;
      client->cv_.notify_all();
    }
  } break;

  default: break;
  }

  return 0;
}
} // namespace Ndmspc
