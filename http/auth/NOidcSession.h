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
  std::string subject;           ///< "sub" claim of the verified token
  std::string preferredUsername; ///< "preferred_username" claim (empty when absent)
  std::chrono::system_clock::time_point expiresAt; ///< Token expiry ("exp" claim)
};

/**
 * @brief Verified caller identity for a single authenticated HTTP request.
 *
 * Same data as NOidcIdentity but named for request-session use in HTTP auth.
 */
struct NOidcSession {
  std::string subject;           ///< "sub" claim of the verified token
  std::string username;          ///< preferred_username, falls back to subject
  std::chrono::system_clock::time_point expiresAt; ///< Token expiry ("exp" claim)

  /**
   * @brief Build a session from a verified identity.
   * @param identity Verified caller identity.
   * @return Session carrying the identity's subject, username and expiry.
   */
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
  /**
   * @brief Outcome of authenticating a request.
   */
  enum class Status {
    Authenticated,       ///< a valid token was presented and verified
    NoCredentials,       ///< no usable Authorization header
    MalformedHeader,     ///< header present but not "Bearer <token>"
    Invalid,             ///< token rejected (signature, issuer, audience, ...)
    Expired,             ///< token is past its expiry
    ProviderUnavailable  ///< the identity provider could not be reached
  };

  Status         status{Status::NoCredentials}; ///< Outcome of the authentication attempt
  NOidcSession   session;                       ///< Verified session (valid when status is Authenticated)
  std::string    errorCode; ///< stable wire code, e.g. "authentication_required", "token_expired"
  bool           retryable{false};   ///< Whether the client may retry the same request
  std::string    diagnostic;         ///< Human-readable detail for logging (not sent to clients)
};

} // namespace Ndmspc

#endif // NDMSPC_NOIDC_SESSION_H
