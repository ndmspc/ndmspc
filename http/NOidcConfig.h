#ifndef NDMSPC_NOIDC_CONFIG_H
#define NDMSPC_NOIDC_CONFIG_H

#include <chrono>
#include <string>

namespace Ndmspc {

struct NOidcConfig {
  std::string issuer;
  std::string audience;
  std::string caFile;
  std::chrono::seconds clockSkew{30};
  std::chrono::seconds jwksRefresh{300};
  std::chrono::seconds jwksMaxStale{86400};
  std::chrono::seconds authenticationTimeout{15};
  std::chrono::milliseconds httpTimeout{5000};
  bool allowInsecureHttp{false};

  bool Enabled() const;
  void Normalize();
  void Validate() const;
};

} // namespace Ndmspc

#endif
