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
static int lws_callback_client_impl(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in,
                                    size_t len);

namespace Ndmspc {

struct WS_URI {
  std::string fScheme;
  std::string fHost;
  int         fPort;
  std::string fPath;
};

class NWsClient {
  public:
  static constexpr const char * fgProtocolName = "ndmspc-protocol";

  NWsClient(int maxRetries = 5, int retryDelayMs = 1000);
  ~NWsClient();

  bool Connect(const std::string & uriString);
  void Disconnect();
  bool Send(const std::string & message);
  bool IsConnected() const { return fConnected.load(); }

  using OnMessageCallback = std::function<void(const std::string &)>;
  void SetOnMessageCallback(OnMessageCallback callback) { fOnMessageCallback = callback; }

  static WS_URI ParseUri(const std::string & uriString);

  private:
  struct lws_context * fLwsContext;
  struct lws *         fWsi;
  std::thread          fLwsServiceThread;
  int                  fMaxRetries;
  int                  fRetryDelayMs;

  std::string fHost;
  int         fPort;
  std::string fPath;

  std::atomic<bool> fConnected;
  std::atomic<bool> fShutdownRequested;
  std::atomic<bool> fConnectionAttemptComplete;

  std::queue<std::string>    fOutgoingMessageQueue;
  std::mutex                 fOutgoingMutex;
  std::vector<unsigned char> fSendBuffer;

  OnMessageCallback fOnMessageCallback;

  static lws_protocols fProtocols[];

  std::mutex              fConnectMutex;
  std::condition_variable fConnectCv;

  void LwsServiceLoop();

  friend int ::lws_callback_client_impl(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in,
                                        size_t len);
};

} // namespace Ndmspc

#endif // NDMSPC_WEBSOCKET_CLIENT_H
