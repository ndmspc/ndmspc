#ifndef NDMSPC_NOIDC_AUTHENTICATOR_H
#define NDMSPC_NOIDC_AUTHENTICATOR_H

#include "NOidcConfig.h"
#include "NOidcSession.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace Ndmspc {

enum class NOidcErrorCode {
  None,
  InvalidToken,
  ExpiredToken,
  NotYetValid,
  InvalidIssuer,
  InvalidAudience,
  UnsupportedAlgorithm,
  UnknownKey,
  ProviderUnavailable
};

struct NOidcResult {
  std::optional<NOidcIdentity> identity;
  NOidcErrorCode error{NOidcErrorCode::None};
  std::string diagnostic;

  explicit operator bool() const { return identity.has_value(); }
};

class IOidcTokenVerifier {
  public:
  virtual ~IOidcTokenVerifier() = default;
  virtual NOidcResult Verify(std::string_view token) = 0;
};

struct NOidcHttpResult {
  int status{0};
  std::string body;
};

class IOidcHttpClient {
  public:
  virtual ~IOidcHttpClient() = default;
  virtual NOidcHttpResult Get(const std::string & url) = 0;
};

class NOidcHttpClient : public IOidcHttpClient {
  public:
  explicit NOidcHttpClient(NOidcConfig config);
  NOidcHttpResult Get(const std::string & url) override;

  private:
  NOidcConfig fConfig;
};

class NKeycloakOidcAuthenticator : public IOidcTokenVerifier {
  public:
  using Clock = std::function<std::chrono::system_clock::time_point()>;

  NKeycloakOidcAuthenticator(NOidcConfig config, std::shared_ptr<IOidcHttpClient> httpClient = nullptr,
                             Clock clock = std::chrono::system_clock::now);
  ~NKeycloakOidcAuthenticator() override;

  void Initialize();
  NOidcResult Verify(std::string_view token) override;

  private:
  bool RefreshKeys(std::string * error);
  bool EnsureKey(const std::string & keyId, std::string & publicKey, std::string * error);
  void RefreshLoop();

  NOidcConfig fConfig;
  std::shared_ptr<IOidcHttpClient> fHttpClient;
  Clock fClock;
  std::string fJwksUri;
  std::map<std::string, std::string> fPublicKeys;
  std::chrono::system_clock::time_point fKeysFetchedAt{};
  std::mutex fMutex;
  std::condition_variable fRefreshCv;
  bool fRefreshing{false};
  bool fStopping{false};
  std::thread fRefreshThread;
};

const char * NOidcErrorCodeName(NOidcErrorCode code);

} // namespace Ndmspc

#endif
