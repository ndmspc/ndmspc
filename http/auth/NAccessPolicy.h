#ifndef NDMSPC_NACCESS_POLICY_H
#define NDMSPC_NACCESS_POLICY_H

#include "NRequestIdentity.h"

#include <string>
#include <vector>

namespace Ndmspc {

/**
 * @brief Who may enter the deployment, after authentication.
 *
 * A user who has authenticated (an OIDC bearer token or an x509 client certificate) is let in only
 * when their identity is on a configured list. An **empty** list allows everyone, which is the
 * default: the feature is inert unless the deployment sets the variable.
 *
 * The list is matched with {@link NRequestIdentity::Matches}, so an entry matches whichever form the
 * user is known by - user name, email or subject - case-insensitively. A value that matches none of
 * them is refused, and the configured message is what they are shown.
 *
 * Configuration comes from the environment:
 *
 *  - `NDMSPC_ALLOWED_USERS` - comma-separated; empty or unset means *everyone*.
 *  - `NDMSPC_ACCESS_DENIED_MESSAGE` - what a refused user is told.
 */
class NAccessPolicy {
  public:
  /// @brief What a refused user is told when the deployment configured no message.
  static const char * DefaultMessage();

  /// @brief The policy the process was started with (see the class note for the variables).
  static NAccessPolicy FromEnvironment();

  NAccessPolicy() = default;

  /**
   * @brief Build a policy from an explicit list and message (used by tests).
   *
   * @param allowed The permitted identities; empty means everyone is allowed.
   * @param message What a refused user is told; empty falls back to {@link DefaultMessage}.
   */
  NAccessPolicy(std::vector<std::string> allowed, std::string message);

  /// @brief Whether an allowlist is in force. False means everyone is allowed.
  bool Configured() const { return !fAllowed.empty(); }

  /// @brief Whether `identity` may enter: true when no list is set, else any entry matches it.
  bool Allows(const NRequestIdentity & identity) const;

  /// @brief What a refused user is told.
  const std::string & Message() const { return fMessage; }

  /// @brief The configured entries (for logging; empty when everyone is allowed).
  const std::vector<std::string> & Allowed() const { return fAllowed; }

  private:
  std::vector<std::string> fAllowed; ///< The permitted identities; empty means everyone is allowed
  std::string              fMessage = DefaultMessage(); ///< What a refused user is told
};

} // namespace Ndmspc

#endif // NDMSPC_NACCESS_POLICY_H
