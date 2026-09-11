#ifndef NDMSPC_NOIDC_TOKEN_CLIENT_H
#define NDMSPC_NOIDC_TOKEN_CLIENT_H

#include <chrono>
#include <memory>
#include <string>

namespace Ndmspc {

/**
 * @brief Configuration for obtaining a Keycloak access token via the OAuth2
 * token endpoint using either the client-credentials or password grant.
 */
struct NOidcTokenClientConfig {
  enum class Grant { ClientCredentials, Password };

  std::string issuer;       ///< OIDC issuer, e.g. "https://keycloak/realms/ndmspc".
  std::string clientId;     ///< OAuth2 client identifier.
  std::string clientSecret; ///< OAuth2 client secret (may be empty for public clients).
  std::string username;     ///< Resource owner username (password grant only).
  std::string password;     ///< Resource owner password (password grant only).
  Grant       grant{Grant::ClientCredentials}; ///< Chosen OAuth2 grant.
  std::string caFile;       ///< CA bundle used to verify the issuer's TLS cert.
  std::string caPath;       ///< Directory of hashed CAs used to verify the issuer's TLS cert.
  bool        allowInsecureHttp{false}; ///< Allow http:// issuer (development only).

  /// Whether a token can be obtained with the current configuration.
  bool Enabled() const { return !issuer.empty() && !clientId.empty(); }
  void Normalize();
  void Validate() const;
};

struct NOidcTokenHttpResult {
  int status{0};
  std::string body;
};

/**
 * @brief Minimal HTTP client used by NOidcTokenClient so tests can inject a fake.
 */
class IOidcTokenHttpClient {
  public:
  virtual ~IOidcTokenHttpClient() = default;
  virtual NOidcTokenHttpResult Post(const std::string & url, const std::string & body, const std::string & contentType) = 0;
};

/**
 * @brief httplib-backed HTTP client for the OIDC token endpoint.
 */
class NOidcHttpClientImpl : public IOidcTokenHttpClient {
  public:
  explicit NOidcHttpClientImpl(NOidcTokenClientConfig config);
  NOidcTokenHttpResult Post(const std::string & url, const std::string & body, const std::string & contentType) override;

  private:
  NOidcTokenClientConfig fConfig;
};

/**
 * @brief Obtains Keycloak access tokens from an OIDC token endpoint.
 *
 * POSTs to `{issuer}/protocol/openid-connect/token` with an
 * `application/x-www-form-urlencoded` body, parses the JSON response and returns
 * the `access_token`. The HTTP layer is injectable for testing.
 */
class NOidcTokenClient {
  public:
  NOidcTokenClient(NOidcTokenClientConfig config, std::shared_ptr<IOidcTokenHttpClient> httpClient = nullptr);

  /**
   * @brief Perform the token request and return the access token.
   * @return The `access_token` from the token endpoint response.
   * @throws std::runtime_error on network or authentication failure.
   */
  std::string ObtainAccessToken();

  private:
  NOidcTokenClientConfig fConfig;
  std::shared_ptr<IOidcTokenHttpClient> fHttpClient;
};

} // namespace Ndmspc

#endif // NDMSPC_NOIDC_TOKEN_CLIENT_H