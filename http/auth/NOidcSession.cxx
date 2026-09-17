#include "NOidcSession.h"

namespace Ndmspc {

NOidcSession NOidcSession::FromIdentity(const NOidcIdentity & identity)
{
  return {identity.subject, identity.preferredUsername, identity.expiresAt};
}

} // namespace Ndmspc
