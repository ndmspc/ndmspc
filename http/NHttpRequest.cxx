#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

#include <TBase64.h>
#include <TString.h>
#include <httplib.h>

#include "ndmspc/core/NLogger.h"
#include "NHttpRequest.h"

namespace {
// Splits a URL into the pieces cpp-httplib needs. Note: only the non-SSL
// Client has a URL-parsing constructor; SSLClient takes a bare host and port,
// so the scheme must be stripped here (passing "https://host:port" as the host
// makes every TLS request fail to resolve).
struct UrlParts {
  bool        https{false};
  std::string host;  // host only: no scheme, no port (IPv6 kept bracketed)
  int         port{80};
  std::string path;  // path and query, starting with '/'
};

UrlParts ParseUrl(const std::string & url)
{
  const auto schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) throw std::runtime_error("NHttpRequest: URL has no scheme: " + url);

  UrlParts    parts;
  parts.https = (url.compare(0, schemeEnd, "https") == 0);

  const std::string rest      = url.substr(schemeEnd + 3);
  const auto        slash     = rest.find('/');
  const std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  parts.path                  = (slash == std::string::npos) ? "/" : rest.substr(slash);

  if (!authority.empty() && authority.front() == '[') {
    // IPv6 literal, e.g. [::1]:5001
    const auto closingBracket = authority.find(']');
    if (closingBracket == std::string::npos) throw std::runtime_error("NHttpRequest: malformed IPv6 host: " + url);
    parts.host = authority.substr(0, closingBracket + 1);
    if (closingBracket + 1 < authority.size() && authority[closingBracket + 1] == ':') {
      parts.port = std::atoi(authority.substr(closingBracket + 2).c_str());
    }
  }
  else {
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
      parts.host = authority.substr(0, colon);
      parts.port = std::atoi(authority.substr(colon + 1).c_str());
    }
    else {
      parts.host = authority;
    }
  }
  if (parts.port <= 0) parts.port = parts.https ? 443 : 80;
  return parts;
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

std::string UpperCase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

// Runs a callback with a configured httplib client for the URL. SSLClient is
// used for HTTPS (with optional mutual TLS client certificate and an optional
// CA bundle/directory for verifying the server).
template <typename Fn>
auto WithClient(const UrlParts & parts, const std::string & cert_path, const std::string & key_path,
                const std::string & key_password_file, bool insecure, const std::string & ca_file,
                const std::string & ca_path, Fn && fn)
{
  if (parts.https) {
    // Only use the client certificate (and its password) when both files are given.
    const bool         use_cert = !cert_path.empty() && !key_path.empty();
    httplib::SSLClient client(parts.host, parts.port, cert_path, key_path,
                              use_cert ? ReadKeyPassword(key_password_file) : std::string());
    client.enable_server_certificate_verification(!insecure);
    if (!ca_file.empty() || !ca_path.empty()) client.set_ca_cert_path(ca_file, ca_path);
    client.set_follow_location(true);
    client.set_connection_timeout(10);
    client.set_read_timeout(60);
    return fn(client);
  }
  httplib::Client client(parts.host, parts.port);
  client.set_follow_location(true);
  client.set_connection_timeout(10);
  client.set_read_timeout(60);
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
  auto result = WithClient(parts, cert_path, key_path, key_password_file, insecure, "", "",
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
  auto result = WithClient(parts, cert_path, key_path, key_password_file, insecure, "", "",
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
  auto result = WithClient(parts, cert_path, key_path, key_password_file, insecure, "", "",
                           [&](auto & client) { return client.Head(parts.path); });
  if (!result) {
    throw std::runtime_error("NHttpRequest HEAD '" + url + "' failed: " + httplib::to_string(result.error()));
  }
  return result->status;
}

NHttpResponse Ndmspc::NHttpRequest::request(const std::string & method, const std::string & url,
                                            const std::string & body,
                                            const std::map<std::string, std::string> & headers,
                                            const std::string & cert_path, const std::string & key_path,
                                            const std::string & key_password_file, const std::string & ca_file,
                                            const std::string & ca_path, bool insecure)
{
  const auto parts = ParseUrl(url);
  const auto verb  = UpperCase(method);

  // cpp-httplib takes the content type as a separate argument (it must not also
  // appear in the header map), so pull it out here, defaulting to JSON.
  std::string      contentType = "application/json";
  httplib::Headers httpHeaders;
  for (const auto & header : headers) {
    if (UpperCase(header.first) == "CONTENT-TYPE") {
      contentType = header.second;
      continue;
    }
    httpHeaders.emplace(header.first, header.second);
  }

  auto result = WithClient(parts, cert_path, key_path, key_password_file, insecure, ca_file, ca_path,
                           [&](auto & client) {
                             if (verb == "GET") return client.Get(parts.path, httpHeaders);
                             if (verb == "HEAD") return client.Head(parts.path, httpHeaders);
                             if (verb == "POST") return client.Post(parts.path, httpHeaders, body, contentType);
                             if (verb == "PUT") return client.Put(parts.path, httpHeaders, body, contentType);
                             if (verb == "PATCH") return client.Patch(parts.path, httpHeaders, body, contentType);
                             if (verb == "DELETE") return client.Delete(parts.path, httpHeaders, body, contentType);
                             throw std::runtime_error("NHttpRequest: unsupported method '" + method + "'");
                           });
  if (!result) {
    throw std::runtime_error("NHttpRequest " + verb + " '" + url + "' failed: " + httplib::to_string(result.error()));
  }

  NHttpResponse response;
  response.status = result->status;
  response.body   = result->body;
  return response;
}
} // namespace Ndmspc
