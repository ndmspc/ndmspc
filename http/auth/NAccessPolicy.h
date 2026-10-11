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
 *  - `NDMSPC_ALLOWED_USERS_MODE` - `guest` (the default) admits a non-listed user read-only; `deny`
 *    refuses them outright.
 *  - `NDMSPC_ACCESS_DENIED_MESSAGE` - what a refused user is told (only meaningful in `deny` mode).
 */
class NAccessPolicy {
  public:
  /**
   * @brief What happens to an authenticated user who is not on the list.
   *
   * `Guest` (the default) lets them in read-only; `Deny` refuses them outright.
   */
  enum class Mode { Guest, Deny };

  /// @brief What a refused user is told when the deployment configured no message.
  static const char * DefaultMessage();

  /// @brief The policy the process was started with (see the class note for the variables).
  static NAccessPolicy FromEnvironment();

  NAccessPolicy() = default;

  /**
   * @brief Build a policy from an explicit list, message and mode (used by tests).
   *
   * @param allowed The permitted identities; empty means everyone is allowed.
   * @param message What a refused user is told; empty falls back to {@link DefaultMessage}.
   * @param mode What happens to a non-listed user (default `Guest`).
   */
  NAccessPolicy(std::vector<std::string> allowed, std::string message, Mode mode = Mode::Guest);

  /// @brief Whether an allowlist is in force. False means everyone is allowed.
  bool Configured() const { return !fAllowed.empty(); }

  /// @brief What happens to an authenticated user who is not on the list.
  Mode GetMode() const { return fMode; }

  /**
   * @brief Whether a non-listed user is refused outright (as opposed to admitted read-only).
   *
   * Has meaning only when the policy is {@link Configured}.
   */
  bool IsDeny() const { return fMode == Mode::Deny; }

  /**
   * @brief `identity`'s role: `"owner"` when it may write, `"guest"` when it may only read.
   *
   * Everyone is an owner when the policy is unconfigured (the list is empty).
   */
  std::string RoleOf(const NRequestIdentity & identity) const;

  /// @brief Whether `identity` is a guest (the policy is configured and it is not on the list).
  bool IsGuest(const NRequestIdentity & identity) const;

  /// @brief Whether `identity` may enter: true when no list is set, else any entry matches it.
  bool Allows(const NRequestIdentity & identity) const;

  /// @brief What a refused user is told.
  const std::string & Message() const { return fMessage; }

  /// @brief The configured entries (for logging; empty when everyone is allowed).
  const std::vector<std::string> & Allowed() const { return fAllowed; }

  private:
  std::vector<std::string> fAllowed; ///< The permitted identities; empty means everyone is allowed
  std::string              fMessage = DefaultMessage(); ///< What a refused user is told
  Mode                     fMode = Mode::Guest;        ///< What a non-listed user gets (guest or deny)
};

} // namespace Ndmspc

#endif // NDMSPC_NACCESS_POLICY_H
