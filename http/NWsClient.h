#ifndef NDMSPC_WEBSOCKET_CLIENT_H
#define NDMSPC_WEBSOCKET_CLIENT_H

#include <functional>
#include <memory>
#include <string>

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
 * @brief WebSocket client for communication with NDMSPC servers, built on cpp-httplib.
 *
 * Handles connection management, message sending/receiving and a background reader
 * thread. Supports plain `ws://` and TLS `wss://` connections, X509 client certificates
 * (mutual TLS) and OAuth2/OIDC first-frame authentication.
 *
 * Unlike an event-loop client, httplib reads are blocking: a dedicated reader thread
 * consumes incoming frames and dispatches them through the message callback, while
 * Send() is synchronous (and internally serialized by httplib).
 */
class NWsClient {
  public:
  static constexpr const char * fgProtocolName = "ndmspc-protocol"; ///< WebSocket subprotocol

  /**
   * @brief Constructor.
   * @param maxRetries Maximum number of connection retries.
   * @param retryDelayMs Delay between retries in milliseconds.
   */
  NWsClient(int maxRetries = 5, int retryDelayMs = 1000);

  /**
   * @brief Destructor. Disconnects and joins the reader thread.
   */
  ~NWsClient();

  NWsClient(const NWsClient &) = delete;
  NWsClient & operator=(const NWsClient &) = delete;

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
   * @return True if the message was sent.
   */
  bool Send(const std::string & message);

  /**
   * @brief Check if the client is currently connected.
   * @return True if connected.
   */
  bool IsConnected() const;

  /// Callback type for received messages.
  using OnMessageCallback = std::function<void(const std::string &)>;

  /**
   * @brief Set the OAuth2/OIDC access token used to authenticate the WebSocket.
   *
   * When set (before Connect()), the client sends the server's standard
   * `{"event":"authenticate","token":"<token>"}` frame immediately after the
   * WebSocket handshake and waits for the `authenticated` acknowledgement.
   * @param token Access token to present to the server.
   */
  void SetAuthenticationToken(const std::string & token);

  /**
   * @brief Whether the connection has successfully authenticated (received the
   * server's `authenticated` frame). Only meaningful when SetAuthenticationToken
   * was used; otherwise the connection is considered authenticated on connect.
   */
  bool IsAuthenticated() const;

  /**
   * @brief Set the callback to be invoked when a message is received.
   * @param callback Function to call with received message.
   */
  void SetOnMessageCallback(OnMessageCallback callback);

  /**
   * @brief Configure client certificate (mutual TLS / client auth) credentials.
   *
   * Must be called before Connect(). The certificate and key are read into memory
   * and presented to the server over `wss://`.
   * @param certFilePath PEM certificate the client presents to the server.
   * @param keyFilePath PEM private key for certFilePath.
   * @param keyPassword Optional passphrase for the private key (empty if none).
   */
  void SetClientCertificate(const std::string & certFilePath, const std::string & keyFilePath,
                            const std::string & keyPassword = "");

  /**
   * @brief Configure the CA bundle used to verify the server certificate.
   *
   * Must be called before Connect(). When empty, the system trust store is used.
   * @param caFilePath PEM file containing the trusted CA certificate(s).
   */
  void SetCaFile(const std::string & caFilePath);

  /**
   * @brief Configure a hashed CA directory used to verify the server certificate.
   *
   * Must be called before Connect(). Ignored when SetCaFile() is also configured,
   * because httplib loads either the CA file or the CA directory for the client.
   * @param caDirPath Directory of hashed CA certificates (grid-style).
   */
  void SetCaPath(const std::string & caDirPath);

  /**
   * @brief Control how the server certificate is validated.
   *
   * Must be called before Connect().
   * @param verifyServer Whether to verify the server certificate chain (default true).
   * @param allowSelfSigned Whether to tolerate missing/self-signed server CAs.
   * @param skipHostnameCheck Whether to skip hostname verification.
   */
  void SetServerVerification(bool verifyServer, bool allowSelfSigned = false, bool skipHostnameCheck = false);

  /**
   * @brief Parse a WebSocket URI string into its components.
   * @param uriString URI string to parse.
   * @return Parsed WS_URI structure.
   */
  static WS_URI ParseUri(const std::string & uriString);

  private:
  struct Impl;

  bool CreateClient(const std::string & url, bool isSecure);
  bool PerformAuthHandshake();
  void ReaderLoop();
  void HandleIncoming(const std::string & message);

  std::unique_ptr<Impl> fImpl; ///< Implementation state (keeps httplib out of this header)
};

} // namespace Ndmspc

#endif // NDMSPC_WEBSOCKET_CLIENT_H
