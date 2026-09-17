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

/**
 * @brief Stable error codes produced when verifying an OIDC token.
 */
enum class NOidcErrorCode {
  None,                 ///< No error (verification succeeded)
  InvalidToken,         ///< Token is malformed or its signature is invalid
  ExpiredToken,         ///< Token is past its expiry
  NotYetValid,          ///< Token is used before its not-before time
  InvalidIssuer,        ///< Token issuer does not match the configuration
  InvalidAudience,      ///< Token audience does not match the configuration
  UnsupportedAlgorithm, ///< Token uses an algorithm other than RS256
  UnknownKey,           ///< No usable key was found for the token's key id
  ProviderUnavailable   ///< The identity provider (JWKS) could not be reached
};

/**
 * @brief Result of verifying a single token.
 */
struct NOidcResult {
  std::optional<NOidcIdentity> identity;         ///< Verified identity, set when verification succeeded
  NOidcErrorCode error{NOidcErrorCode::None};    ///< Error code (None on success)
  std::string diagnostic;                        ///< Human-readable detail for logging

  /// @brief Whether verification succeeded (an identity is present).
  explicit operator bool() const { return identity.has_value(); }
};

/**
 * @brief Interface for verifying OIDC/OAuth2 access tokens.
 */
class IOidcTokenVerifier {
  public:
  virtual ~IOidcTokenVerifier() = default;
  /**
   * @brief Verify a raw access token.
   * @param token Raw JWT to verify.
   * @return The verification result (identity on success, error code otherwise).
   */
  virtual NOidcResult Verify(std::string_view token) = 0;
};

/**
 * @brief Raw HTTP response returned by an IOidcHttpClient.
 */
struct NOidcHttpResult {
  int status{0};    ///< HTTP status code
  std::string body; ///< Raw response body
};

/**
 * @brief Minimal HTTP client used by the authenticator so tests can inject a fake.
 */
class IOidcHttpClient {
  public:
  virtual ~IOidcHttpClient() = default;
  /**
   * @brief GET a URL.
   * @param url Absolute request URL.
   * @return The raw HTTP response.
   */
  virtual NOidcHttpResult Get(const std::string & url) = 0;
};

/**
 * @brief httplib-backed HTTP client used to fetch OIDC discovery/JWKS documents.
 */
class NOidcHttpClient : public IOidcHttpClient {
  public:
  /**
   * @brief Constructor.
   * @param config OIDC configuration supplying timeouts and TLS trust.
   */
  explicit NOidcHttpClient(NOidcConfig config);
  /**
   * @brief GET a URL.
   * @param url Absolute request URL.
   * @return The raw HTTP response.
   * @throws std::runtime_error on network failure.
   */
  NOidcHttpResult Get(const std::string & url) override;

  private:
  NOidcConfig fConfig; ///< Configuration used for timeouts and TLS trust
};

/**
 * @brief Keycloak (OIDC) token verifier backed by the issuer's JWKS.
 *
 * Performs OIDC discovery on Initialize(), caches the issuer's RS256 signing keys
 * and refreshes them both on demand (EnsureKey) and periodically (RefreshLoop).
 */
class NKeycloakOidcAuthenticator : public IOidcTokenVerifier {
  public:
  /// @brief Injectable clock returning the current time (overridable for tests).
  using Clock = std::function<std::chrono::system_clock::time_point()>;

  /**
   * @brief Constructor.
   * @param config OIDC configuration (issuer, audience, timeouts, TLS trust).
   * @param httpClient Injectable HTTP client; a default httplib-backed one is created when null.
   * @param clock Injectable clock; defaults to the system clock.
   */
  NKeycloakOidcAuthenticator(NOidcConfig config, std::shared_ptr<IOidcHttpClient> httpClient = nullptr,
                             Clock clock = std::chrono::system_clock::now);
  ~NKeycloakOidcAuthenticator() override;

  /**
   * @brief Fetch the OIDC discovery document and start the background key refresh.
   * @throws std::runtime_error when discovery fails or no usable signing keys are found.
   */
  void Initialize();
  /**
   * @brief Verify a raw RS256 JWT against the issuer's keys.
   * @param token Raw JWT to verify.
   * @return The verification result (identity on success, error code otherwise).
   */
  NOidcResult Verify(std::string_view token) override;

  private:
  /**
   * @brief Fetch and cache the issuer's JWKS signing keys.
   * @param error Set to a human-readable message on failure (may be null).
   * @return True when the keys were refreshed successfully.
   */
  bool RefreshKeys(std::string * error);
  /**
   * @brief Return the public key for a key id, refreshing the JWKS when it is stale.
   * @param keyId Key id ("kid") from the token header.
   * @param publicKey Output: PEM-encoded public key.
   * @param error Set to a human-readable message on failure (may be null).
   * @return True when a fresh key is available.
   */
  bool EnsureKey(const std::string & keyId, std::string & publicKey, std::string * error);
  /// @brief Background loop refreshing the keys every NOidcConfig::jwksRefresh.
  void RefreshLoop();

  NOidcConfig fConfig;                              ///< OIDC configuration
  std::shared_ptr<IOidcHttpClient> fHttpClient;     ///< HTTP transport for discovery/JWKS
  Clock fClock;                                     ///< Clock used for staleness/expiry checks
  std::string fJwksUri;                             ///< JWKS endpoint from the discovery document
  std::map<std::string, std::string> fPublicKeys;   ///< Cached PEM public keys by key id
  std::chrono::system_clock::time_point fKeysFetchedAt{}; ///< Time of the last successful key fetch
  std::mutex fMutex;                                ///< Guards fPublicKeys/fKeysFetchedAt/fRefreshing/fStopping
  std::condition_variable fRefreshCv;               ///< Signals refresh completion or shutdown
  bool fRefreshing{false};                          ///< True while a key refresh is in progress
  bool fStopping{false};                            ///< True when the refresh thread should exit
  std::thread fRefreshThread;                       ///< Background key refresh thread
};

/**
 * @brief Get the stable wire name of an error code.
 * @param code The error code.
 * @return A stable, lower-case string (e.g. "token_expired").
 */
const char * NOidcErrorCodeName(NOidcErrorCode code);

} // namespace Ndmspc

#endif
