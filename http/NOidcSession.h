#ifndef NDMSPC_NOIDC_SESSION_H
#define NDMSPC_NOIDC_SESSION_H

#include <chrono>
#include <string>

namespace Ndmspc {

/**
 * @brief Verified caller identity, as returned by the token verifier.
 *
 * Transport-agnostic: produced by IOidcTokenVerifier and consumed by the HTTP
 * authentication middleware and the WebSocket handler.
 */
struct NOidcIdentity {
  std::string subject;
  std::string preferredUsername;
  std::chrono::system_clock::time_point expiresAt;
};

/**
 * @brief Verified caller identity for a single authenticated HTTP request.
 *
 * Same data as NOidcIdentity but named for request-session use in HTTP auth.
 */
struct NOidcSession {
  std::string subject;
  std::string username; ///< preferred_username, falls back to subject
  std::chrono::system_clock::time_point expiresAt;

  static NOidcSession FromIdentity(const NOidcIdentity & identity);
};

/**
 * @brief Result of authenticating a single HTTP request.
 *
 * Used by the ROOT THttpCallArg integration (NOidcHttpAuthenticator) and by a
 * future standalone (non-ROOT) HTTP server, which can map Status directly to
 * HTTP codes.
 */
struct NHttpAuthResult {
  enum class Status {
    Authenticated,
    NoCredentials,   ///< no usable Authorization header
    MalformedHeader, ///< header present but not "Bearer <token>"
    Invalid,         ///< token rejected (signature, issuer, audience, ...)
    Expired,         ///< token is past its expiry
    ProviderUnavailable
  };

  Status         status{Status::NoCredentials};
  NOidcSession   session;
  std::string    errorCode; ///< stable wire code, e.g. "authentication_required", "token_expired"
  bool           retryable{false};
  std::string    diagnostic;
};

} // namespace Ndmspc

#endif // NDMSPC_NOIDC_SESSION_H
