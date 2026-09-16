#ifndef NDMSPC_NOIDC_CONFIG_H
#define NDMSPC_NOIDC_CONFIG_H

#include <chrono>
#include <string>

namespace Ndmspc {

/**
 * @brief Configuration for OIDC (Keycloak) token verification.
 *
 * All timeouts and intervals are expressed as std::chrono durations. Authentication
 * is disabled only when both `issuer` and `audience` are empty (see Enabled()).
 */
struct NOidcConfig {
  std::string issuer;   ///< OIDC issuer URL, e.g. "https://keycloak/realms/ndmspc"
  std::string audience; ///< Expected token audience (required when authentication is enabled)
  std::string caFile;   ///< PEM file with the CA bundle used to verify the issuer's TLS certificate
  std::string caPath;   ///< Directory of hashed CAs used to verify the issuer's TLS certificate
  std::chrono::seconds clockSkew{30};             ///< Allowed clock skew when checking token times
  std::chrono::seconds jwksRefresh{300};          ///< Interval between JWKS key refreshes
  std::chrono::seconds jwksMaxStale{86400};       ///< Maximum age of cached JWKS keys before refresh is forced
  std::chrono::seconds authenticationTimeout{15}; ///< Budget for a WebSocket authentication handshake
  std::chrono::milliseconds httpTimeout{5000};    ///< Timeout for HTTP calls to the issuer
  bool allowInsecureHttp{false};                  ///< Allow an http:// issuer (development only)

  /// @brief Whether OIDC authentication is configured (issuer or audience is set).
  bool Enabled() const;
  /// @brief Normalize the configuration in place (strips trailing slashes from the issuer).
  void Normalize();
  /**
   * @brief Validate the configuration.
   *
   * A no-op when authentication is disabled. Otherwise requires both issuer and
   * audience, an https:// issuer (unless allowInsecureHttp), positive timeouts with
   * jwksMaxStale >= jwksRefresh, no whitespace in issuer/audience and readable CA
   * file/directory paths when set.
   *
   * @throws std::invalid_argument when the configuration is inconsistent.
   */
  void Validate() const;
};

} // namespace Ndmspc

#endif
