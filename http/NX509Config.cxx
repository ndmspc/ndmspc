#include "NX509Config.h"

#include <filesystem>
#include <stdexcept>

namespace Ndmspc {

void NX509Config::Validate() const
{
  if (!Enabled()) return;
  if (certFile.empty() || keyFile.empty()) throw std::invalid_argument("X509 cert and key must both be configured");
  const auto mustBeFile = [](const std::string & path, const char * name) {
    if (path.empty() || !std::filesystem::is_regular_file(path)) {
      throw std::invalid_argument(std::string("X509 ") + name + " is not a readable file");
    }
  };
  mustBeFile(certFile, "certificate file");
  mustBeFile(keyFile, "private key file");
  if (!caFile.empty()) mustBeFile(caFile, "CA file");
  if (!caPath.empty() && !std::filesystem::is_directory(caPath)) {
    throw std::invalid_argument("X509 CA path is not a readable directory");
  }
  if (caFile.empty() && caPath.empty() && !verifyOptional) {
    throw std::invalid_argument("X509 CA file or CA path is required when client certificates are mandatory");
  }
  if (identity != "cn" && identity != "dn") throw std::invalid_argument("X509 identity must be 'cn' or 'dn'");
  if (internalPort <= 0 || internalPort > 65535) throw std::invalid_argument("X509 internal port must be in range 1..65535");
}

} // namespace Ndmspc