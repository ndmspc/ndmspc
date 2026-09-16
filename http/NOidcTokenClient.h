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
  /// @brief OAuth2 grant used to obtain the token.
  enum class Grant {
    ClientCredentials, ///< machine-to-machine client credentials grant
    Password           ///< resource owner password credentials grant
  };

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
  /// @brief Normalize the configuration in place (strips trailing slashes from the issuer).
  void Normalize();
  /**
   * @brief Validate the configuration.
   * @throws std::invalid_argument when the issuer/client id are missing, the issuer is not
   *         HTTPS (unless allowInsecureHttp), or the password grant lacks username/password.
   */
  void Validate() const;
};

/**
 * @brief Raw HTTP response returned by an IOidcTokenHttpClient.
 */
struct NOidcTokenHttpResult {
  int status{0};    ///< HTTP status code
  std::string body; ///< Raw response body
};

/**
 * @brief Minimal HTTP client used by NOidcTokenClient so tests can inject a fake.
 */
class IOidcTokenHttpClient {
  public:
  virtual ~IOidcTokenHttpClient() = default;
  /**
   * @brief POST a form-encoded body to a URL.
   * @param url Absolute request URL.
   * @param body Request body (already form-encoded).
   * @param contentType Content-Type header value.
   * @return The raw HTTP response.
   */
  virtual NOidcTokenHttpResult Post(const std::string & url, const std::string & body, const std::string & contentType) = 0;
};

/**
 * @brief httplib-backed HTTP client for the OIDC token endpoint.
 */
class NOidcHttpClientImpl : public IOidcTokenHttpClient {
  public:
  /**
   * @brief Constructor.
   * @param config Token client configuration (normalized and validated).
   */
  explicit NOidcHttpClientImpl(NOidcTokenClientConfig config);
  /**
   * @brief POST a form-encoded body to a URL.
   * @param url Absolute request URL.
   * @param body Request body (already form-encoded).
   * @param contentType Content-Type header value.
   * @return The raw HTTP response.
   * @throws std::runtime_error on network failure.
   */
  NOidcTokenHttpResult Post(const std::string & url, const std::string & body, const std::string & contentType) override;

  private:
  NOidcTokenClientConfig fConfig; ///< Configuration used for timeouts and TLS trust.
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
  /**
   * @brief Constructor.
   * @param config Token client configuration (normalized and validated).
   * @param httpClient Injectable HTTP client; a default httplib-backed one is created when null.
   */
  NOidcTokenClient(NOidcTokenClientConfig config, std::shared_ptr<IOidcTokenHttpClient> httpClient = nullptr);

  /**
   * @brief Perform the token request and return the access token.
   * @return The `access_token` from the token endpoint response.
   * @throws std::runtime_error on network or authentication failure.
   */
  std::string ObtainAccessToken();

  private:
  NOidcTokenClientConfig fConfig;     ///< Token client configuration
  std::shared_ptr<IOidcTokenHttpClient> fHttpClient; ///< HTTP transport used for token requests
};

} // namespace Ndmspc

#endif // NDMSPC_NOIDC_TOKEN_CLIENT_H