#ifndef NDMSPC_WEBSOCKET_CLIENT_H
#define NDMSPC_WEBSOCKET_CLIENT_H

#include <libwebsockets.h>

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <functional>

// Forward declaration of the global LWS callback function.
/**
 * @brief Global callback function for libwebsockets client events.
 *
 * Handles various websocket events such as connection, message receipt, errors, etc.
 *
 * @param wsi Pointer to the websocket instance.
 * @param reason Callback reason/event type.
 * @param user User data pointer.
 * @param in Input data pointer.
 * @param len Length of input data.
 * @return Status code for libwebsockets.
 */
static int lws_callback_client_impl(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in,
                                    size_t len);

namespace Ndmspc {

/**
 * @brief Structure representing a parsed WebSocket URI.
 */
struct WS_URI {
  std::string fScheme; ///< URI scheme (e.g., "ws", "wss")
  std::string fHost;   ///< Hostname or IP address
  int         fPort;   ///< Port number
  std::string fPath;   ///< Path component
};

/**
 * @class NWsClient
 * @brief WebSocket client for asynchronous communication using libwebsockets.
 *
 * Handles connection management, message sending/receiving, and threading.
 */
class NWsClient {
  public:
  static constexpr const char * fgProtocolName = "ndmspc-protocol"; ///< Protocol name for websocket communication

  /**
   * @brief Constructor.
   * @param maxRetries Maximum number of connection retries.
   * @param retryDelayMs Delay between retries in milliseconds.
   */
  NWsClient(int maxRetries = 5, int retryDelayMs = 1000);

  /**
   * @brief Destructor. Cleans up resources and disconnects.
   */
  ~NWsClient();

  /**
   * @brief Connect to a WebSocket server.
   * @param uriString URI string to connect to.
   * @return True if connection is successful.
   */
  bool Connect(const std::string & uriString);

  /**
   * @brief Disconnect from the WebSocket server.
   */
  void Disconnect();

  /**
   * @brief Send a message to the server.
   * @param message Message string to send.
   * @return True if message was queued for sending.
   */
  bool Send(const std::string & message);

  /**
   * @brief Check if the client is currently connected.
   * @return True if connected.
   */
  bool IsConnected() const { return fConnected.load(); }

  /// Callback type for received messages.
  using OnMessageCallback = std::function<void(const std::string &)>;

  /**
   * @brief Set the callback to be invoked when a message is received.
   * @param callback Function to call with received message.
   */
  void SetOnMessageCallback(OnMessageCallback callback) { fOnMessageCallback = callback; }

  /**
   * @brief Parse a WebSocket URI string into its components.
   * @param uriString URI string to parse.
   * @return Parsed WS_URI structure.
   */
  static WS_URI ParseUri(const std::string & uriString);

  private:
  struct lws_context * fLwsContext;       ///< libwebsockets context
  struct lws *         fWsi;              ///< WebSocket instance
  std::thread          fLwsServiceThread; ///< Thread running the service loop
  int                  fMaxRetries;       ///< Maximum connection retries
  int                  fRetryDelayMs;     ///< Delay between retries (ms)

  std::string fHost; ///< Hostname
  int         fPort; ///< Port number
  std::string fPath; ///< Path

  std::atomic<bool> fConnected;                 ///< Connection status
  std::atomic<bool> fShutdownRequested;         ///< Shutdown flag
  std::atomic<bool> fConnectionAttemptComplete; ///< Connection attempt completion flag

  std::queue<std::string>    fOutgoingMessageQueue; ///< Queue of outgoing messages
  std::mutex                 fOutgoingMutex;        ///< Mutex for outgoing queue
  std::vector<unsigned char> fSendBuffer;           ///< Buffer for sending messages
  std::condition_variable    fSendCv;               ///< Condition variable for sending messages

  OnMessageCallback fOnMessageCallback; ///< Callback for received messages

  static lws_protocols fProtocols[]; ///< Protocols supported by libwebsockets

  std::mutex              fConnectMutex; ///< Mutex for connection state
  std::condition_variable fConnectCv;    ///< Condition variable for connection

  /**
   * @brief Service loop for libwebsockets running in a separate thread.
   */
  void LwsServiceLoop();

  /**
   * @brief Friend declaration for the global callback function.
   */
  friend int ::lws_callback_client_impl(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in,
                                        size_t len);
};

} // namespace Ndmspc

#endif // NDMSPC_WEBSOCKET_CLIENT_H
