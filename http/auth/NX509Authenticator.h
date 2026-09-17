#ifndef NDMSPC_NX509_AUTHENTICATOR_H
#define NDMSPC_NX509_AUTHENTICATOR_H

#include "NX509Config.h"

#include <memory>
#include <string>

// Avoid pulling httplib internals into consumers; expose a thin facade.
namespace httplib {
class SSLServer;
class Server;
class Client;
} // namespace httplib

namespace Ndmspc {

/**
 * @brief X509 mutual-TLS front-door built on httplib.
 *
 * Terminates TLS on the public port, requires/verifies client certificates
 * against the configured CA, and derives the authenticated username from the
 * verified certificate's subject DN (Common Name by default). It then forwards
 * both HTTP API and WebSocket traffic to the internal loopback ROOT-based
 * engine, which runs with OIDC disabled (anonymous internally) so the front
 * door is the sole authentication gate.
 *
 * The X509 mode is mutually exclusive with OIDC; NX509Config::Validate()
 * rejects enabling both.
 */
class NX509Authenticator {
  public:
  /**
   * @brief Construct the mTLS front door. Does not bind until Start().
   * @param config Validated X509 configuration.
   */
  explicit NX509Authenticator(NX509Config config);
  ~NX509Authenticator();

  NX509Authenticator(const NX509Authenticator &) = delete;
  NX509Authenticator &operator=(const NX509Authenticator &) = delete;

  /**
   * @brief Bind the HTTPS listener and register HTTP + WebSocket routing.
   * @param listenHost Host to bind (e.g. "0.0.0.0" or "::").
   * @param port Public port.
   * @param internalBase Base URL of the loopback ROOT engine,
   *        e.g. "http://127.0.0.1:8081".
   * @return true when the listener is running.
   */
  bool Start(const std::string & listenHost, int port, const std::string & internalBase);

  /**
   * @brief Stop the listener if running.
   */
  void Stop();

  /**
   * @brief Whether Start() succeeded and the listener is active.
   */
  bool IsRunning() const;

  /**
   * @brief Extract the identity attribute from a PEM-encoded certificate.
   * @param certPem PEM certificate (can be a complete cert file/chain text).
   * @param identityAttr "cn" (Common Name) or "dn" (full subject DN).
   * @return The extracted identity, or an empty string when unavailable.
   */
  static std::string ExtractIdentity(const std::string & certPem, const std::string & identityAttr);

  private:
  struct Impl; ///< Pimpl holding the httplib server and listener thread
  std::unique_ptr<Impl> fImpl; ///< Implementation state (keeps httplib out of this header)
};

} // namespace Ndmspc

#endif // NDMSPC_NX509_AUTHENTICATOR_H