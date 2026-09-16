#include "NOidcTokenClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <utility>

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
  if (schemeEnd == std::string::npos) throw std::runtime_error("OIDC token URL has no scheme");
  const auto pathStart = url.find('/', schemeEnd + 3);
  if (pathStart == std::string::npos) return {url, "/"};
  return {url.substr(0, pathStart), url.substr(pathStart)};
}

std::string UrlEncode(const std::string & value)
{
  std::string encoded;
  encoded.reserve(value.size());
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      char buf[4];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      encoded += buf;
    }
  }
  return encoded;
}

} // namespace

void NOidcTokenClientConfig::Normalize()
{
  while (issuer.size() > 1 && issuer.back() == '/') issuer.pop_back();
}

void NOidcTokenClientConfig::Validate() const
{
  if (!Enabled()) throw std::invalid_argument("OIDC token issuer and client id are required");
  const bool https = issuer.starts_with("https://");
  const bool http = issuer.starts_with("http://");
  if (!https && !(allowInsecureHttp && http)) throw std::invalid_argument("OIDC token issuer must use HTTPS");
  if (issuer.find_first_of(" \t\r\n") != std::string::npos) throw std::invalid_argument("OIDC token issuer must not contain whitespace");
  if (grant == Grant::Password && (username.empty() || password.empty())) {
    throw std::invalid_argument("OIDC password grant requires a username and password");
  }
}

NOidcHttpClientImpl::NOidcHttpClientImpl(NOidcTokenClientConfig config) : fConfig(std::move(config))
{
  fConfig.Normalize();
  fConfig.Validate();
}

NOidcTokenHttpResult NOidcHttpClientImpl::Post(const std::string & url, const std::string & body,
                                               const std::string & contentType)
{
  const auto parts = ParseUrl(url);
  httplib::Client client(parts.origin);
  client.set_connection_timeout(15);
  client.set_read_timeout(30);
  client.set_follow_location(true);

  if (!fConfig.caFile.empty() || !fConfig.caPath.empty()) client.set_ca_cert_path(fConfig.caFile, fConfig.caPath);

  httplib::Headers headers;
  headers.emplace("Content-Type", contentType);
  headers.emplace("Accept", "application/json");

  auto result = client.Post(parts.path, headers, body, contentType);
  if (!result) {
    throw std::runtime_error("OIDC token request to '" + url + "' failed: " + httplib::to_string(result.error()));
  }
  return {.status = result->status, .body = result->body};
}

NOidcTokenClient::NOidcTokenClient(NOidcTokenClientConfig config, std::shared_ptr<IOidcTokenHttpClient> httpClient)
    : fConfig(std::move(config)), fHttpClient(std::move(httpClient))
{
  if (!fHttpClient) fHttpClient = std::make_shared<NOidcHttpClientImpl>(fConfig);
}

std::string NOidcTokenClient::ObtainAccessToken()
{
  const std::string tokenUrl = fConfig.issuer + "/protocol/openid-connect/token";

  // RFC 6749 token request body.
  std::string body;
  body += "grant_type=";
  body += fConfig.grant == NOidcTokenClientConfig::Grant::Password ? "password" : "client_credentials";
  body += "&client_id=" + UrlEncode(fConfig.clientId);
  if (!fConfig.clientSecret.empty()) body += "&client_secret=" + UrlEncode(fConfig.clientSecret);
  if (fConfig.grant == NOidcTokenClientConfig::Grant::Password)
    body += "&username=" + UrlEncode(fConfig.username) + "&password=" + UrlEncode(fConfig.password);

  const NOidcTokenHttpResult result =
      fHttpClient->Post(tokenUrl, body, "application/x-www-form-urlencoded");

  if (result.status / 100 != 2) {
    throw std::runtime_error("OIDC token request to '" + tokenUrl + "' returned HTTP " + std::to_string(result.status) +
                             ": " + result.body);
  }

  json response;
  try {
    response = json::parse(result.body);
  } catch (const json::parse_error & e) {
    throw std::runtime_error("OIDC token response was not valid JSON: " + std::string(e.what()));
  }
  const auto token = response.find("access_token");
  if (token == response.end() || !token->is_string() || token->get_ref<const std::string &>().empty()) {
    throw std::runtime_error("OIDC token response did not contain an access_token");
  }
  return token->get_ref<const std::string &>();
}

} // namespace Ndmspc