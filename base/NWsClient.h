#ifndef Ndmspc_NWsClient_H
#define Ndmspc_NWsClient_H
#include <libwebsockets.h>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace Ndmspc {

///
/// \class NWsClient
///
/// \brief NWsClient object
///	\author Martin Vala <mvala@cern.ch>
///

class NWsClient {
  public:
  NWsClient();
  virtual ~NWsClient();
  bool        connect(const std::string & url);
  void        disconnect();
  bool        send(const std::string & message);
  std::string receive(int timeout_ms = 5000);
  bool        is_connected() const;

  private:
  // Callback function for libwebsockets
  static int callback_function(struct lws * wsi, enum lws_callback_reasons reason, void * user, void * in, size_t len);

  // Protocol definition
  static struct lws_protocols protocols_[];

  // Member variables
  struct lws_context * context_;
  struct lws *         wsi_;
  std::string          host_;
  bool                 connected_;
  bool                 thread_running_;
  bool                 pending_tx_;

  std::thread             service_thread_;
  std::mutex              mutex_;
  std::condition_variable cv_;

  std::vector<uint8_t>     send_buffer_;
  std::vector<char>        rx_buffer_;         // Use char for binary safety
  std::vector<std::string> received_messages_; // Store complete messages, not fragments
};
} // namespace Ndmspc
#endif
