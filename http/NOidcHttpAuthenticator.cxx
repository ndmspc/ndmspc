#include "NOidcHttpAuthenticator.h"

#include <THttpCallArg.h>

#include "ndmspc/core/NLogger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <vector>

namespace Ndmspc {
namespace {

constexpr const char * kAuthenticationRequiredCode = "authentication_required";

std::string ToLower(std::string_view value)
{
  std::string lower;
  lower.reserve(value.size());
  std::transform(value.begin(), value.end(), std::back_inserter(lower),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower;
}

} // namespace

NHttpAuthResult NOidcHttpAuthenticator::Authenticate(const std::shared_ptr<IOidcTokenVerifier> & verifier,
                                                     std::string_view authorizationHeader)
{
  NHttpAuthResult result;
  result.diagnostic = "Missing Authorization header";

  // Anonymous mode: nothing to check.
  if (!verifier) {
    result.status = NHttpAuthResult::Status::Authenticated;
    result.diagnostic.clear();
    return result;
  }

  if (authorizationHeader.empty()) {
    result.errorCode = kAuthenticationRequiredCode;
    return result;
  }

  // Trim leading whitespace before parsing the scheme.
  std::string_view header(authorizationHeader);
  while (!header.empty() && (header.front() == ' ' || header.front() == '\t')) header.remove_prefix(1);
  if (header.empty()) {
    result.status = NHttpAuthResult::Status::MalformedHeader;
    result.errorCode = "invalid_authorization_header";
    result.diagnostic = "Authorization header contains no token";
    return result;
  }

  // Only the "Bearer" scheme is supported.
  const auto schemeEnd = header.find(' ');
  if (schemeEnd == std::string_view::npos || ToLower(header.substr(0, schemeEnd)) != "bearer") {
    result.status = NHttpAuthResult::Status::MalformedHeader;
    result.errorCode = "invalid_authorization_header";
    result.diagnostic = "Authorization header must use the Bearer scheme";
    return result;
  }

  std::string token(header.substr(schemeEnd + 1));
  // Trim surrounding whitespace.
  const auto first = token.find_first_not_of(" \t");
  const auto last = token.find_last_not_of(" \t");
  if (first == std::string::npos || last < first) {
    result.status = NHttpAuthResult::Status::MalformedHeader;
    result.errorCode = "invalid_authorization_header";
    result.diagnostic = "Authorization header contains no token";
    return result;
  }
  token = token.substr(first, last - first + 1);

  const auto verified = verifier->Verify(token);
  if (!verified) {
    result.diagnostic = verified.diagnostic;
    return MapVerifierFailure(verified);
  }

  result.status = NHttpAuthResult::Status::Authenticated;
  result.session = NOidcSession::FromIdentity(*verified.identity);
  result.diagnostic.clear();
  return result;
}

bool NOidcHttpAuthenticator::ApplyToRequest(const std::shared_ptr<IOidcTokenVerifier> & verifier, THttpCallArg * arg)
{
  if (!arg) return false;

  const auto authHeader = arg->GetRequestHeader("Authorization");
  auto result = Authenticate(verifier, authHeader.Data());

  if (result.status != NHttpAuthResult::Status::Authenticated) {
    // ROOT's civetweb engine cannot send a custom body together with an error
    // status: Set404() discards the content and replies with an empty 404 page.
    // We therefore reply 200 with the JSON error body plus WWW-Authenticate and
    // no-store headers; clients must inspect error.code. Servers with explicit
    // status support (e.g. a future standalone one) use Authenticate() directly
    // to return proper 401/503 codes.
    arg->SetContentType("application/json");
    arg->SetContent(json{{"error", {{"code", result.errorCode},
                                    {"message", result.diagnostic},
                                    {"retryable", result.retryable}}}}.dump());
    arg->AddHeader("WWW-Authenticate", "Bearer");
    arg->AddNoCacheHeader();
    NLogWarning("HTTP request rejected by OIDC authentication: code=%s diagnostic=%s", result.errorCode.c_str(),
                result.diagnostic.c_str());
    return false;
  }

  // Record the verified identity on the argument so handlers can inspect it.
  // In anonymous mode there is no identity to record.
  if (result.session.username.empty() && result.session.subject.empty()) return true;
  arg->SetUserName(result.session.username.c_str());
  arg->AddHeader(kUserHeader, result.session.username.c_str());
  arg->AddHeader(kSubjectHeader, result.session.subject.c_str());
  const auto expires = std::chrono::duration_cast<std::chrono::seconds>(result.session.expiresAt.time_since_epoch()).count();
  arg->AddHeader(kExpiresHeader, std::to_string(expires).c_str());
  arg->AddHeader(kAuthenticatedHeader, result.session.subject.c_str());
  return true;
}

NOidcSession NOidcHttpAuthenticator::GetAuthenticatedSession(THttpCallArg * arg)
{
  NOidcSession session;
  if (!arg) return session;
  if (arg->GetUserName()) session.username = arg->GetUserName();
  const auto subject = arg->GetRequestHeader(kSubjectHeader);
  if (!subject.IsNull()) session.subject = subject.Data();
  const auto expires = arg->GetRequestHeader(kExpiresHeader);
  if (!expires.IsNull()) {
    try {
      session.expiresAt = std::chrono::system_clock::time_point(
          std::chrono::seconds(std::stoll(expires.Data())));
    } catch (...) {
    }
  }
  if (session.subject.empty()) session.subject = session.username;
  if (session.username.empty()) session.username = session.subject;
  return session;
}

NHttpAuthResult NOidcHttpAuthenticator::MapVerifierFailure(const NOidcResult & result)
{
  NHttpAuthResult mapped;
  mapped.diagnostic = result.diagnostic;
  mapped.errorCode = NOidcErrorCodeName(result.error);
  switch (result.error) {
    case NOidcErrorCode::ExpiredToken:
      mapped.status = NHttpAuthResult::Status::Expired;
      break;
    case NOidcErrorCode::ProviderUnavailable:
      mapped.status = NHttpAuthResult::Status::ProviderUnavailable;
      mapped.retryable = true;
      break;
    default:
      mapped.status = NHttpAuthResult::Status::Invalid;
      break;
  }
  return mapped;
}

} // namespace Ndmspc
