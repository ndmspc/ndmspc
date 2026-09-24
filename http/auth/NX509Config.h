#ifndef NDMSPC_NX509_CONFIG_H
#define NDMSPC_NX509_CONFIG_H

#include <string>

namespace Ndmspc {

/**
 * @brief Configuration for X509 client-certificate (mutual TLS) authentication.
 *
 * X509 authentication is a mutually exclusive alternative to the OIDC (Keycloak)
 * bearer-token flow. When enabled, a TLS connection must present a client
 * certificate that verifies against the configured CA(s); the authenticated
 * username is derived from the certificate's subject DN (Common Name by
 * default).
 *
 * The server terminates TLS with fCertFile/fKeyFile, verifies client
 * certificates against fCaFile and/or fCaPath, and serves both the HTTP API and
 * WebSocket connections. The existing ROOT-based HTTP engine is kept on a
 * loopback port (fInternalPort) with OIDC disabled, so handlers remain unchanged.
 */
struct NX509Config {
  std::string certFile;           ///< Server certificate (PEM).
  std::string keyFile;            ///< Server private key (PEM).
  std::string caFile;             ///< Bundle of CAs allowed to sign client certs.
  std::string caPath;             ///< Directory of hashed CAs allowed to sign client certs (grid-style).
  bool verifyOptional{false};     ///< false (default) requires a client cert.
  std::string identity{"cn"};     ///< "cn" (default) or "dn" - which subject part becomes the username.
  int internalPort{8081};         ///< Loopback ROOT engine port.
  /**
   * Allowed CORS origins for browser clients: "*" or a comma-separated list.
   * Empty (default) sends no CORS headers; the request origin is echoed so the
   * identity headers stay readable by credentialed requests too.
   */
  std::string cors;

  /**
   * @brief Whether X509 authentication is enabled for this server.
   */
  bool Enabled() const { return !certFile.empty(); }

  /**
   * @brief Validate the configuration (throws std::invalid_argument on error).
   */
  void Validate() const;
};

} // namespace Ndmspc

#endif // NDMSPC_NX509_CONFIG_H