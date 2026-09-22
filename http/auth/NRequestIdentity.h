#ifndef NDMSPC_NREQUEST_IDENTITY_H
#define NDMSPC_NREQUEST_IDENTITY_H

#include "NOidcSession.h"

#include "ndmspc/core/NLogger.h" ///< provides the global `json` (nlohmann) alias

#include <string>
#include <vector>

class THttpCallArg;

namespace Ndmspc {

/**
 * @brief Who is making a request, as far as the server can tell.
 *
 * Two kinds of identity reach a request, and they are not worth the same:
 *
 *  - **verified**: the server checked a token or a client certificate itself (OIDC bearer
 *    authentication), or an authenticating front door did and forwarded the result to a server that
 *    was told to trust it (the X509 mutual-TLS door, whose engine is loopback-only);
 *  - **asserted**: nothing was checked and the client simply says who it is - the `owner` of a room
 *    request. Useful where there is no login at all, and worth exactly as much as the client's word.
 *
 * `verified` carries that difference, so a caller can choose what to believe: the room router
 * believes a verified identity and only falls back to the assertion when there is none.
 *
 * The identity reaches a handler as the `_identity` key of its input JSON - the same seam the
 * request's query already uses (`_query`) - because a handler is only given its method and its
 * input: there is no request object to read. The server assigns that key itself, after parsing the
 * body, so a client cannot smuggle one in.
 */
struct NRequestIdentity {
  std::string subject;  ///< Verified token subject ("sub" claim); "" when unknown
  std::string username; ///< Verified user name ("preferred_username", or the certificate's name)
  std::string email;    ///< Verified email ("email" claim); "" when the token carries none
  /**
   * Whether a token or a certificate backs the values above.
   *
   * False means the values are the client's own claim (an asserted owner), True means something
   * authenticated them. An asserted identity uses `username` for the claim.
   */
  bool verified{false};

  /// @brief Whether nothing identifies this request at all.
  bool Empty() const { return subject.empty() && username.empty() && email.empty(); }

  /**
   * @brief What this identity is named by: the user name, else the email, else the subject.
   *
   * One string is all a room id, a URL, a Kubernetes label and a resource name have room for, so it
   * is the short, stable one: what a person answers to. It is what a room created on this identity's
   * behalf is called after, and what is recorded as its owner - which is also why the user name comes
   * first here while matching (Identifiers) uses every form.
   */
  std::string Owner() const;

  /**
   * @brief Everything this identity may be matched by, for ownership and admin checks.
   *
   * One person can be known by more than one of these (a token carries both an email and a
   * preferred_username; a certificate only a name), and a room created before they logged in carries
   * whichever was available then - so matching any of them is a match.
   */
  std::vector<std::string> Identifiers() const;

  /// @brief Whether `candidate` equals one of Identifiers() (case-insensitive, surrounding space ignored).
  bool Matches(const std::string & candidate) const;

  /// @brief This identity as the `_identity` value of a request's input JSON.
  json ToJson() const;

  /// @brief Reads an `_identity` value; an absent or malformed one yields an empty identity.
  static NRequestIdentity FromJson(const json & value);

  /// @brief The identity of a verified token session.
  static NRequestIdentity FromSession(const NOidcSession & session);

  /**
   * @brief An identity the client asserted (nothing was verified, so `verified` stays false).
   *
   * A client may know more than one name for itself - a user name and an email, say - and says so:
   * the first names what it creates, and every one of them counts for matching, so an admin list may
   * be written in either.
   *
   * @param owner The name the client answers to ("" for none).
   * @param email Its email, when it sent one ("" otherwise).
   * @return The asserted identity.
   */
  static NRequestIdentity FromAssertion(const std::string & owner, const std::string & email = std::string());

  /// @brief The identity implied by a user name the server already authenticated.
  static NRequestIdentity FromUsername(const std::string & username);

  /**
   * @brief The identity an authenticating front door forwarded in the request headers.
   *
   * The X509 front door verifies the client certificate itself and passes the result on as
   * X-NDMSPC-User / -Subject / -Email, so the ROOT engine behind it can serve per-user requests
   * without authenticating anything itself. Meaningful only for an engine that was told it sits
   * behind that door (where it is loopback-only); anywhere else a client could write those headers.
   *
   * @param arg Request argument to read the headers from.
   * @return The forwarded identity (empty when no such header is present).
   */
  static NRequestIdentity FromForwardedHeaders(THttpCallArg * arg);

  /// @brief Lower-cases a value and drops surrounding whitespace, for the comparisons above.
  static std::string Normalize(const std::string & value);
};

} // namespace Ndmspc

#endif // NDMSPC_NREQUEST_IDENTITY_H
