#include <fstream>
#include <stdexcept>
#include <utility>

#include <TBase64.h>
#include <TString.h>
#include <httplib.h>

#include "ndmspc/core/NLogger.h"
#include "NHttpRequest.h"

namespace {
// Splits a full URL into its origin (scheme://host[:port]) and path components.
// cpp-httplib takes the host and path separately, unlike libcurl's single URL.
struct UrlParts {
  std::string origin;
  std::string path;
};

UrlParts ParseUrl(const std::string & url)
{
  const auto schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) throw std::runtime_error("NHttpRequest: URL has no scheme: " + url);
  const auto pathStart = url.find('/', schemeEnd + 3);
  if (pathStart == std::string::npos) return {url, "/"};
  return {url.substr(0, pathStart), url.substr(pathStart)};
}

// Decodes a base64-encoded key password file into a plaintext password, or
// returns an empty string when the file is empty/unreadable.
std::string ReadKeyPassword(const std::string & key_password_file)
{
  std::ifstream passwordFile(key_password_file);
  if (!passwordFile.is_open()) {
    NLogError("Could not open key password file: %s", key_password_file.c_str());
    return {};
  }
  std::string encodedPassword;
  std::getline(passwordFile, encodedPassword);
  passwordFile.close();

  TString     encoded(encodedPassword);            // Use TString
  TString     decoded = TBase64::Decode(encoded);  // Decode with TBase64
  std::string password = decoded.Data();           // Convert TString back to std::string

  // Remove carriage return if it exists, to avoid authentication failure.
  if (!password.empty() && password.back() == '\r') {
    password.pop_back();
  }

  return password;
}

// Runs a callback with a configured httplib client for the URL. SSLClient is
// used for HTTPS (with optional mutual TLS client certificate).
template <typename Fn>
auto WithClient(const UrlParts & parts, bool https, const std::string & cert_path, const std::string & key_path,
                const std::string & key_password_file, bool insecure, Fn && fn)
{
  if (https) {
    // Only use the client certificate (and its password) when both files are given.
    const bool use_cert = !cert_path.empty() && !key_path.empty();
    httplib::SSLClient client(parts.origin, 443, cert_path, key_path,
                              use_cert ? ReadKeyPassword(key_password_file) : std::string());
    client.enable_server_certificate_verification(!insecure);
    client.set_follow_location(true);
    client.set_connection_timeout(10);
    return fn(client);
  }
  httplib::Client client(parts.origin);
  client.set_follow_location(true);
  client.set_connection_timeout(10);
  return fn(client);
}
} // namespace

namespace Ndmspc {
NHttpRequest::NHttpRequest() {}

Ndmspc::NHttpRequest::~NHttpRequest() {}

std::string Ndmspc::NHttpRequest::get(const std::string & url, const std::string & cert_path,
                                      const std::string & key_path, const std::string & key_password_file,
                                      bool insecure)
{
  const auto parts = ParseUrl(url);
  httplib::Headers headers;
  headers.emplace("Content-Type", "application/json");
  auto result = WithClient(parts, url.starts_with("https://"), cert_path, key_path, key_password_file, insecure,
                           [&](auto & client) { return client.Get(parts.path, headers); });
  if (!result) {
    throw std::runtime_error("NHttpRequest GET '" + url + "' failed: " + httplib::to_string(result.error()));
  }
  return result->body;
}

std::string Ndmspc::NHttpRequest::post(const std::string & url, const std::string & post_data,
                                       const std::string & cert_path, const std::string & key_path,
                                       const std::string & key_password_file, bool insecure)
{
  const auto parts = ParseUrl(url);
  auto result = WithClient(parts, url.starts_with("https://"), cert_path, key_path, key_password_file, insecure,
                           [&](auto & client) { return client.Post(parts.path, post_data, "application/json"); });
  if (!result) {
    throw std::runtime_error("NHttpRequest POST '" + url + "' failed: " + httplib::to_string(result.error()));
  }
  return result->body;
}

int Ndmspc::NHttpRequest::head(const std::string & url, const std::string & cert_path, const std::string & key_path,
                               const std::string & key_password_file, bool insecure)
{
  const auto parts = ParseUrl(url);
  auto result = WithClient(parts, url.starts_with("https://"), cert_path, key_path, key_password_file, insecure,
                           [&](auto & client) { return client.Head(parts.path); });
  if (!result) {
    throw std::runtime_error("NHttpRequest HEAD '" + url + "' failed: " + httplib::to_string(result.error()));
  }
  return result->status;
}
} // namespace Ndmspc