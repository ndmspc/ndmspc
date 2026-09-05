#include "NOidcAuthenticator.h"

#include <httplib.h>
#include <jwt-cpp/jwt.h>
#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Ndmspc {
namespace {

using json = nlohmann::json;

struct UrlParts {
  std::string origin;
  std::string path;
};

UrlParts ParseUrl(const std::string & url)
{
  const auto schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) throw std::runtime_error("URL has no scheme");
  const auto pathStart = url.find('/', schemeEnd + 3);
  if (pathStart == std::string::npos) return {url, "/"};
  return {url.substr(0, pathStart), url.substr(pathStart)};
}

std::vector<unsigned char> DecodeBase64Url(std::string value)
{
  std::replace(value.begin(), value.end(), '-', '+');
  std::replace(value.begin(), value.end(), '_', '/');
  while (value.size() % 4 != 0) value.push_back('=');
  std::vector<unsigned char> out((value.size() / 4) * 3);
  const int size = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char *>(value.data()), value.size());
  if (size < 0) throw std::runtime_error("Invalid base64url value");
  auto padding = static_cast<int>(std::count(value.begin(), value.end(), '='));
  out.resize(static_cast<size_t>(size - padding));
  return out;
}

std::string RsaJwkToPem(const json & jwk)
{
  if (jwk.value("kty", "") != "RSA" || !jwk.contains("n") || !jwk.contains("e")) {
    throw std::runtime_error("Unsupported JWK");
  }
  const auto modulus = DecodeBase64Url(jwk.at("n").get<std::string>());
  const auto exponent = DecodeBase64Url(jwk.at("e").get<std::string>());
  BIGNUM * n = BN_bin2bn(modulus.data(), modulus.size(), nullptr);
  BIGNUM * e = BN_bin2bn(exponent.data(), exponent.size(), nullptr);
  OSSL_PARAM_BLD * builder = OSSL_PARAM_BLD_new();
  EVP_PKEY_CTX * context = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
  OSSL_PARAM * params = nullptr;
  EVP_PKEY * key = nullptr;
  BIO * bio = nullptr;
  if (!n || !e || !builder || !context || OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_RSA_N, n) != 1 ||
      OSSL_PARAM_BLD_push_BN(builder, OSSL_PKEY_PARAM_RSA_E, e) != 1 || !(params = OSSL_PARAM_BLD_to_param(builder)) ||
      EVP_PKEY_fromdata_init(context) != 1 || EVP_PKEY_fromdata(context, &key, EVP_PKEY_PUBLIC_KEY, params) != 1 ||
      !(bio = BIO_new(BIO_s_mem())) || PEM_write_bio_PUBKEY(bio, key) != 1) {
    if (bio) BIO_free(bio);
    EVP_PKEY_free(key);
    OSSL_PARAM_free(params);
    EVP_PKEY_CTX_free(context);
    OSSL_PARAM_BLD_free(builder);
    BN_free(n);
    BN_free(e);
    throw std::runtime_error("Cannot convert RSA JWK");
  }
  char * data = nullptr;
  const auto size = BIO_get_mem_data(bio, &data);
  std::string pem(data, static_cast<size_t>(size));
  BIO_free(bio);
  EVP_PKEY_free(key);
  OSSL_PARAM_free(params);
  EVP_PKEY_CTX_free(context);
  OSSL_PARAM_BLD_free(builder);
  BN_free(n);
  BN_free(e);
  return pem;
}

bool IsAllowedUrl(const std::string & url, bool allowInsecureHttp)
{
  return url.starts_with("https://") || (allowInsecureHttp && url.starts_with("http://"));
}

NOidcResult Failure(NOidcErrorCode code, std::string diagnostic)
{
  return {.identity = std::nullopt, .error = code, .diagnostic = std::move(diagnostic)};
}

} // namespace

NOidcHttpClient::NOidcHttpClient(NOidcConfig config) : fConfig(std::move(config)) {}

NOidcHttpResult NOidcHttpClient::Get(const std::string & url)
{
  const auto parts = ParseUrl(url);
  httplib::Client client(parts.origin);
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(fConfig.httpTimeout);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(fConfig.httpTimeout - seconds);
  client.set_connection_timeout(seconds.count(), micros.count());
  client.set_read_timeout(seconds.count(), micros.count());
  client.set_write_timeout(seconds.count(), micros.count());
  client.set_follow_location(false);
  client.enable_server_certificate_verification(true);
  if (!fConfig.caFile.empty()) client.set_ca_cert_path(fConfig.caFile);
  const auto response = client.Get(parts.path);
  if (!response) {
    throw std::runtime_error("OIDC request to '" + url + "' failed: " + httplib::to_string(response.error()));
  }
  return {.status = response->status, .body = response->body};
}

NKeycloakOidcAuthenticator::NKeycloakOidcAuthenticator(NOidcConfig config,
                                                       std::shared_ptr<IOidcHttpClient> httpClient, Clock clock)
    : fConfig(std::move(config)), fHttpClient(std::move(httpClient)), fClock(std::move(clock))
{
  fConfig.Normalize();
  fConfig.Validate();
  if (!fHttpClient) fHttpClient = std::make_shared<NOidcHttpClient>(fConfig);
}

NKeycloakOidcAuthenticator::~NKeycloakOidcAuthenticator()
{
  {
    std::lock_guard lock(fMutex);
    fStopping = true;
  }
  fRefreshCv.notify_all();
  if (fRefreshThread.joinable()) fRefreshThread.join();
}

void NKeycloakOidcAuthenticator::Initialize()
{
  const auto discovery = fHttpClient->Get(fConfig.issuer + "/.well-known/openid-configuration");
  if (discovery.status != 200) throw std::runtime_error("OIDC discovery returned HTTP " + std::to_string(discovery.status));
  const auto document = json::parse(discovery.body);
  if (document.value("issuer", "") != fConfig.issuer) throw std::runtime_error("OIDC discovery issuer does not match configuration");
  fJwksUri = document.value("jwks_uri", "");
  if (!IsAllowedUrl(fJwksUri, fConfig.allowInsecureHttp)) throw std::runtime_error("OIDC JWKS URI is not allowed");
  std::string error;
  if (!RefreshKeys(&error)) throw std::runtime_error(error);
  fRefreshThread = std::thread(&NKeycloakOidcAuthenticator::RefreshLoop, this);
}

bool NKeycloakOidcAuthenticator::RefreshKeys(std::string * error)
{
  try {
    const auto response = fHttpClient->Get(fJwksUri);
    if (response.status != 200) throw std::runtime_error("OIDC JWKS returned HTTP " + std::to_string(response.status));
    const auto document = json::parse(response.body);
    std::map<std::string, std::string> keys;
    for (const auto & key : document.value("keys", json::array())) {
      if (key.value("kty", "") != "RSA" || key.value("alg", "RS256") != "RS256" ||
          (key.contains("use") && key.value("use", "") != "sig")) continue;
      const auto keyId = key.value("kid", "");
      if (!keyId.empty()) keys.emplace(keyId, RsaJwkToPem(key));
    }
    if (keys.empty()) throw std::runtime_error("OIDC JWKS contains no usable RS256 keys");
    {
      std::lock_guard lock(fMutex);
      fPublicKeys = std::move(keys);
      fKeysFetchedAt = fClock();
    }
    return true;
  }
  catch (const std::exception & exception) {
    if (error) *error = exception.what();
    return false;
  }
}

bool NKeycloakOidcAuthenticator::EnsureKey(const std::string & keyId, std::string & publicKey, std::string * error)
{
  {
    std::unique_lock lock(fMutex);
    if (const auto it = fPublicKeys.find(keyId); it != fPublicKeys.end() &&
        fClock() - fKeysFetchedAt <= fConfig.jwksMaxStale) {
      publicKey = it->second;
      return true;
    }
    if (fRefreshing) {
      fRefreshCv.wait(lock, [this] { return !fRefreshing; });
      if (const auto it = fPublicKeys.find(keyId); it != fPublicKeys.end() &&
          fClock() - fKeysFetchedAt <= fConfig.jwksMaxStale) {
        publicKey = it->second;
        return true;
      }
      if (error) *error = "OIDC signing key is unknown or stale";
      return false;
    }
    fRefreshing = true;
  }
  std::string refreshError;
  const bool refreshed = RefreshKeys(&refreshError);
  {
    std::lock_guard lock(fMutex);
    fRefreshing = false;
    if (const auto it = fPublicKeys.find(keyId); it != fPublicKeys.end() &&
        fClock() - fKeysFetchedAt <= fConfig.jwksMaxStale) {
      publicKey = it->second;
    }
  }
  fRefreshCv.notify_all();
  if (!publicKey.empty()) return true;
  if (error) *error = refreshed ? "OIDC signing key is unknown" : refreshError;
  return false;
}

NOidcResult NKeycloakOidcAuthenticator::Verify(std::string_view token)
{
  try {
    const auto decoded = jwt::decode(std::string(token));
    if (decoded.get_algorithm() != "RS256") return Failure(NOidcErrorCode::UnsupportedAlgorithm, "Unsupported JWT algorithm");
    if (!decoded.has_header_claim("kid")) return Failure(NOidcErrorCode::UnknownKey, "JWT has no key ID");
    const auto keyId = decoded.get_header_claim("kid").as_string();
    if (keyId.empty()) return Failure(NOidcErrorCode::UnknownKey, "JWT has an empty key ID");
    std::string publicKey;
    std::string keyError;
    if (!EnsureKey(keyId, publicKey, &keyError)) {
      std::lock_guard lock(fMutex);
      if (fClock() - fKeysFetchedAt > fConfig.jwksMaxStale) return Failure(NOidcErrorCode::ProviderUnavailable, keyError);
      return Failure(NOidcErrorCode::UnknownKey, keyError);
    }
    jwt::algorithm::rs256 algorithm(publicKey, "", "", "");
    std::error_code signatureError;
    algorithm.verify(decoded.get_header_base64() + "." + decoded.get_payload_base64(), decoded.get_signature(), signatureError);
    if (signatureError) return Failure(NOidcErrorCode::InvalidToken, signatureError.message());
    if (!decoded.has_issuer() || decoded.get_issuer() != fConfig.issuer) return Failure(NOidcErrorCode::InvalidIssuer, "JWT issuer mismatch");
    if (!decoded.has_audience() || !decoded.get_audience().contains(fConfig.audience)) {
      return Failure(NOidcErrorCode::InvalidAudience, "JWT audience does not contain expected value '" + fConfig.audience + "'");
    }
    if (!decoded.has_subject() || decoded.get_subject().empty()) return Failure(NOidcErrorCode::InvalidToken, "JWT subject is missing");
    if (!decoded.has_expires_at()) return Failure(NOidcErrorCode::InvalidToken, "JWT expiry is missing");
    const auto now = fClock();
    const auto skew = fConfig.clockSkew;
    if (decoded.get_expires_at() + skew < now) return Failure(NOidcErrorCode::ExpiredToken, "JWT is expired");
    if (decoded.has_not_before() && decoded.get_not_before() - skew > now) return Failure(NOidcErrorCode::NotYetValid, "JWT is not active");
    if (decoded.has_issued_at() && decoded.get_issued_at() - skew > now) return Failure(NOidcErrorCode::NotYetValid, "JWT issue time is in the future");
    auto username = decoded.get_subject();
    if (decoded.has_payload_claim("preferred_username")) {
      const auto claim = decoded.get_payload_claim("preferred_username");
      if (claim.get_type() == jwt::json::type::string && !claim.as_string().empty()) username = claim.as_string();
    }
    return {.identity = NOidcIdentity{decoded.get_subject(), username, decoded.get_expires_at()},
            .error = NOidcErrorCode::None, .diagnostic = {}};
  }
  catch (const std::exception & exception) {
    return Failure(NOidcErrorCode::InvalidToken, exception.what());
  }
}

void NKeycloakOidcAuthenticator::RefreshLoop()
{
  std::unique_lock lock(fMutex);
  while (!fStopping) {
    if (fRefreshCv.wait_for(lock, fConfig.jwksRefresh, [this] { return fStopping; })) break;
    if (fRefreshing) continue;
    fRefreshing = true;
    lock.unlock();
    std::string error;
    RefreshKeys(&error);
    lock.lock();
    fRefreshing = false;
    fRefreshCv.notify_all();
  }
}

const char * NOidcErrorCodeName(NOidcErrorCode code)
{
  switch (code) {
    case NOidcErrorCode::None: return "none";
    case NOidcErrorCode::InvalidToken: return "invalid_token";
    case NOidcErrorCode::ExpiredToken: return "token_expired";
    case NOidcErrorCode::NotYetValid: return "not_yet_valid";
    case NOidcErrorCode::InvalidIssuer: return "invalid_issuer";
    case NOidcErrorCode::InvalidAudience: return "invalid_audience";
    case NOidcErrorCode::UnsupportedAlgorithm: return "unsupported_algorithm";
    case NOidcErrorCode::UnknownKey: return "unknown_key";
    case NOidcErrorCode::ProviderUnavailable: return "provider_unavailable";
  }
  return "invalid_token";
}

} // namespace Ndmspc
