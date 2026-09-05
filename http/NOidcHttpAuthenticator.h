#ifndef NDMSPC_NOIDC_HTTP_AUTHENTICATOR_H
#define NDMSPC_NOIDC_HTTP_AUTHENTICATOR_H

#include "NOidcSession.h"
#include "NOidcAuthenticator.h"

#include <memory>
#include <string_view>

class THttpCallArg;
class TString;

namespace Ndmspc {

/**
 * @brief HTTP bearer-token authentication built on IOidcTokenVerifier.
 *
 * The core (Authenticate) is transport-neutral and can be used directly by any
 * HTTP server, including a future standalone (non-ROOT) one:
 *
 * @code
 * auto result = NOidcHttpAuthenticator::Authenticate(verifier, authorizationHeader);
 * if (result.status != NHttpAuthResult::Status::Authenticated) { rejectRequest(result); }
 * @endcode
 *
 * ApplyToRequest() additionally binds the result to a ROOT THttpCallArg:
 * on success it records the verified identity on the argument; on failure it
 * writes an error JSON body and auth headers.
 */
class NOidcHttpAuthenticator {
  public:
  /**
   * @brief Validate an Authorization header value against the token verifier.
   * @param verifier Shared token verifier (may be null in anonymous mode).
   * @param authorizationHeader Raw value of the Authorization header.
   * @return NHttpAuthResult describing the outcome.
   */
  static NHttpAuthResult Authenticate(const std::shared_ptr<IOidcTokenVerifier> & verifier,
                                      std::string_view authorizationHeader);

  /**
   * @brief Apply bearer authentication to a ROOT HTTP request argument.
   *
   * On success returns true and records the verified identity via
   * SetUserName() and X-NDMSPC-* headers. On failure returns false and writes a
   * JSON error response (with WWW-Authenticate) into the argument.
   *
   * @param verifier Shared token verifier (may be null in anonymous mode).
   * @param arg ROOT request/response argument.
   * @return true when the request is authenticated (or anonymous mode).
   */
  static bool ApplyToRequest(const std::shared_ptr<IOidcTokenVerifier> & verifier, THttpCallArg * arg);

  /**
   * @brief Headers set on the response argument after successful authentication.
   */
  static constexpr const char * kUserHeader = "X-NDMSPC-User";
  static constexpr const char * kSubjectHeader = "X-NDMSPC-Subject";
  static constexpr const char * kExpiresHeader = "X-NDMSPC-Token-Expires";
  static constexpr const char * kAuthenticatedHeader = "X-NDMSPC-Authenticated";

  /**
   * @brief Read the verified identity recorded on a request argument.
   *
   * Only meaningful after ApplyToRequest() returned true (or when the argument
   * was built internally from an authenticated WebSocket connection).
   */
  static NOidcSession GetAuthenticatedSession(THttpCallArg * arg);

  private:
  static NHttpAuthResult MapVerifierFailure(const NOidcResult & result);
};

} // namespace Ndmspc

#endif // NDMSPC_NOIDC_HTTP_AUTHENTICATOR_H
