#include "NOidcSession.h"

namespace Ndmspc {

NOidcSession NOidcSession::FromIdentity(const NOidcIdentity & identity)
{
  return {.subject = identity.subject,
          .username = identity.preferredUsername,
          .email = identity.email,
          .expiresAt = identity.expiresAt};
}

} // namespace Ndmspc
