#include "NOidcConfig.h"

#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace Ndmspc {

bool NOidcConfig::Enabled() const
{
  return !issuer.empty() || !audience.empty();
}

void NOidcConfig::Normalize()
{
  while (issuer.size() > 1 && issuer.back() == '/') issuer.pop_back();
}

void NOidcConfig::Validate() const
{
  if (!Enabled()) return;
  if (issuer.empty() || audience.empty()) throw std::invalid_argument("OIDC issuer and audience must both be configured");
  const bool https = issuer.starts_with("https://");
  const bool http = issuer.starts_with("http://");
  if (!https && !(allowInsecureHttp && http)) throw std::invalid_argument("OIDC issuer must use HTTPS");
  if (issuer.find_first_of(" \t\r\n") != std::string::npos) throw std::invalid_argument("OIDC issuer must not contain whitespace");
  if (audience.find_first_of(" \t\r\n") != std::string::npos) throw std::invalid_argument("OIDC audience must not contain whitespace");
  if (clockSkew.count() < 0 || jwksRefresh.count() <= 0 || jwksMaxStale.count() <= 0 ||
      authenticationTimeout.count() <= 0 || httpTimeout.count() <= 0) {
    throw std::invalid_argument("OIDC timeout and refresh values must be positive");
  }
  if (jwksMaxStale < jwksRefresh) throw std::invalid_argument("OIDC JWKS max stale interval must not be shorter than refresh interval");
  if (!caFile.empty() && !std::filesystem::is_regular_file(caFile)) throw std::invalid_argument("OIDC CA file is not readable");
}

} // namespace Ndmspc
