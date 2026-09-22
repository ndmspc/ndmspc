#include "NRequestIdentity.h"

#include <THttpCallArg.h>

#include "NOidcHttpAuthenticator.h"

#include <cctype>

namespace Ndmspc {
namespace {

/// @brief One value of an `_identity` object, or "" when it is absent or not a string.
std::string StringField(const json & value, const char * key)
{
  if (!value.is_object()) return {};
  const auto field = value.find(key);
  return (field != value.end() && field->is_string()) ? field->get<std::string>() : std::string();
}

/// @brief Whether an `_identity` object says the values were verified (absent means they were not).
bool BoolField(const json & value, const char * key)
{
  if (!value.is_object()) return false;
  const auto field = value.find(key);
  return field != value.end() && field->is_boolean() && field->get<bool>();
}

/// @brief A forwarded identity header, or "" when the request does not carry it.
std::string ForwardedHeader(THttpCallArg * arg, const char * name)
{
  if (arg == nullptr) return {};
  const auto value = arg->GetRequestHeader(name);
  return value.IsNull() ? std::string() : std::string(value.Data());
}

} // namespace

std::string NRequestIdentity::Normalize(const std::string & value)
{
  std::string normalized;
  normalized.reserve(value.size());
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return normalized;
  const auto last = value.find_last_not_of(" \t\r\n");
  for (auto i = first; i <= last; ++i) {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value[i]))));
  }
  return normalized;
}

std::string NRequestIdentity::Owner() const
{
  if (!username.empty()) return username;
  if (!email.empty()) return email;
  return subject;
}

std::vector<std::string> NRequestIdentity::Identifiers() const
{
  std::vector<std::string> identifiers;
  for (const std::string * value : {&email, &username, &subject}) {
    if (!value->empty()) identifiers.push_back(*value);
  }
  return identifiers;
}

bool NRequestIdentity::Matches(const std::string & candidate) const
{
  const std::string wanted = Normalize(candidate);
  if (wanted.empty()) return false;
  for (const auto & value : Identifiers()) {
    if (Normalize(value) == wanted) return true;
  }
  return false;
}

json NRequestIdentity::ToJson() const
{
  return json{{"user", username}, {"email", email}, {"subject", subject}, {"verified", verified}};
}

NRequestIdentity NRequestIdentity::FromJson(const json & value)
{
  NRequestIdentity identity;
  if (!value.is_object()) return identity;
  identity.username = StringField(value, "user");
  identity.email    = StringField(value, "email");
  identity.subject  = StringField(value, "subject");
  identity.verified = BoolField(value, "verified");
  return identity;
}

NRequestIdentity NRequestIdentity::FromSession(const NOidcSession & session)
{
  NRequestIdentity identity;
  identity.subject  = session.subject;
  identity.username = session.username;
  identity.email    = session.email;
  // A session exists only because a token was verified.
  identity.verified = !identity.Empty();
  return identity;
}

NRequestIdentity NRequestIdentity::FromAssertion(const std::string & owner, const std::string & email)
{
  NRequestIdentity identity;
  identity.username = owner;
  identity.email    = email;
  identity.verified = false;
  return identity;
}

NRequestIdentity NRequestIdentity::FromUsername(const std::string & username)
{
  NRequestIdentity identity;
  identity.username = username;
  // A user name only ever appears on an argument that something authenticated: the OIDC middleware,
  // the WebSocket handshake, or the front door.
  identity.verified = !identity.Empty();
  return identity;
}

NRequestIdentity NRequestIdentity::FromForwardedHeaders(THttpCallArg * arg)
{
  NRequestIdentity identity;
  identity.username = ForwardedHeader(arg, NOidcHttpAuthenticator::kUserHeader);
  identity.subject  = ForwardedHeader(arg, NOidcHttpAuthenticator::kSubjectHeader);
  identity.email    = ForwardedHeader(arg, NOidcHttpAuthenticator::kEmailHeader);
  // The front door verified the client certificate before writing these headers.
  identity.verified = !identity.Empty();
  return identity;
}

} // namespace Ndmspc
