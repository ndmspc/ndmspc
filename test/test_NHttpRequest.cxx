// Unit tests for NHttpRequest's timeout override.
//
// A room restoring its session before serving a request needs the fetch to give up quickly:
// on the defaults (10s connect / 60s read) a cold or busy router would stall the client for
// up to a minute. SetTimeout is what lets it bound that, so it is worth pinning down.
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <stdexcept>
#include <string>

#include "ndmspc/http/NHttpRequest.h"

namespace {

/// @brief A listening socket that accepts connections but never replies.
class SilentPeer {
  public:
  SilentPeer()
  {
    fListener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fListener < 0) return;

    const int reuse = 1;
    ::setsockopt(fListener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    if (::bind(fListener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) return;
    if (::listen(fListener, 1) != 0) return;

    socklen_t length = sizeof(address);
    if (::getsockname(fListener, reinterpret_cast<sockaddr *>(&address), &length) != 0) return;
    fPort = ntohs(address.sin_port);
  }

  ~SilentPeer()
  {
    if (fListener >= 0) ::close(fListener);
  }

  bool Ok() const { return fListener >= 0 && fPort > 0; }
  std::string Url() const { return "http://127.0.0.1:" + std::to_string(fPort) + "/never"; }

  private:
  int fListener{-1};
  int fPort{0};
};

} // namespace

TEST(NHttpRequestTimeoutTest, GivesUpOnASilentPeerWithinTheRequestedTimeout)
{
  SilentPeer peer;
  ASSERT_TRUE(peer.Ok()) << "could not set up a silent peer";

  Ndmspc::NHttpRequest http;
  http.SetTimeout(1000, 1500);

  const auto start = std::chrono::steady_clock::now();
  bool       failed = false;
  try {
    http.request("GET", peer.Url());
  }
  catch (const std::exception &) {
    failed = true;
  }
  const long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

  EXPECT_TRUE(failed) << "a silent peer must fail rather than hang";
  // Well under the 60s default: the point of the override is that the wait is bounded by what
  // the caller asked for.
  EXPECT_LT(elapsedMs, 20000) << "the read timeout should have bounded the wait, took " << elapsedMs << "ms";
}
