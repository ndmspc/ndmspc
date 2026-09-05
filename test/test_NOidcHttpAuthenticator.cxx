#include <gtest/gtest.h>
#include <THttpCallArg.h>
#include "ndmspc/http/NOidcHttpAuthenticator.h"

#include <chrono>
#include <deque>
#include <memory>

namespace {

class FakeVerifier : public Ndmspc::IOidcTokenVerifier {
  public:
  Ndmspc::NOidcResult Verify(std::string_view token) override
  {
    tokens.emplace_back(token);
    if (results.empty()) return {.identity = std::nullopt, .error = Ndmspc::NOidcErrorCode::InvalidToken, .diagnostic = "rejected"};
    auto result = results.front();
    results.pop_front();
    return result;
  }

  std::deque<Ndmspc::NOidcResult> results;
  std::vector<std::string> tokens;
};

Ndmspc::NOidcResult Identity(std::string subject, std::string username)
{
  return {.identity = Ndmspc::NOidcIdentity{std::move(subject), std::move(username),
                                            std::chrono::system_clock::now() + std::chrono::minutes(5)},
          .error = Ndmspc::NOidcErrorCode::None, .diagnostic = {}};
}

Ndmspc::NOidcResult Failure(Ndmspc::NOidcErrorCode code, std::string diagnostic = "failure")
{
  return {.identity = std::nullopt, .error = code, .diagnostic = std::move(diagnostic)};
}

TEST(NOidcHttpAuthenticatorTest, AnonymousModeAcceptsWithoutHeader)
{
  const auto result = Ndmspc::NOidcHttpAuthenticator::Authenticate(nullptr, "");
  EXPECT_EQ(result.status, Ndmspc::NHttpAuthResult::Status::Authenticated);
}

TEST(NOidcHttpAuthenticatorTest, MissingHeaderRequiresAuthentication)
{
  auto verifier = std::make_shared<FakeVerifier>();
  const auto result = Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "");
  EXPECT_EQ(result.status, Ndmspc::NHttpAuthResult::Status::NoCredentials);
  EXPECT_EQ(result.errorCode, "authentication_required");
}

TEST(NOidcHttpAuthenticatorTest, MalformedHeaderRejected)
{
  auto verifier = std::make_shared<FakeVerifier>();
  EXPECT_EQ(Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Basic abc123").status,
            Ndmspc::NHttpAuthResult::Status::MalformedHeader);
  EXPECT_EQ(Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Bearer").status,
            Ndmspc::NHttpAuthResult::Status::MalformedHeader);
  EXPECT_EQ(Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Bearer   ").status,
            Ndmspc::NHttpAuthResult::Status::MalformedHeader);
}

TEST(NOidcHttpAuthenticatorTest, ValidBearerTokenAuthenticates)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Identity("subject-1", "alice"));
  const auto result = Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Bearer access-token");
  ASSERT_EQ(result.status, Ndmspc::NHttpAuthResult::Status::Authenticated);
  EXPECT_EQ(result.session.subject, "subject-1");
  EXPECT_EQ(result.session.username, "alice");
  ASSERT_EQ(verifier->tokens.size(), 1);
  EXPECT_EQ(verifier->tokens.front(), "access-token");
}

TEST(NOidcHttpAuthenticatorTest, MixedCaseBearerAndWhitespaceAccepted)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Identity("subject-1", "alice"));
  const auto result = Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "  bEaReR   token-value  ");
  ASSERT_EQ(result.status, Ndmspc::NHttpAuthResult::Status::Authenticated);
  ASSERT_EQ(verifier->tokens.size(), 1);
  EXPECT_EQ(verifier->tokens.front(), "token-value");
}

TEST(NOidcHttpAuthenticatorTest, MapsVerifierFailures)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Failure(Ndmspc::NOidcErrorCode::ExpiredToken));
  const auto expired = Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Bearer t");
  EXPECT_EQ(expired.status, Ndmspc::NHttpAuthResult::Status::Expired);
  EXPECT_EQ(expired.errorCode, "token_expired");

  verifier->results.push_back(Failure(Ndmspc::NOidcErrorCode::ProviderUnavailable, "provider down"));
  const auto provider = Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Bearer t");
  EXPECT_EQ(provider.status, Ndmspc::NHttpAuthResult::Status::ProviderUnavailable);
  EXPECT_TRUE(provider.retryable);

  verifier->results.push_back(Failure(Ndmspc::NOidcErrorCode::InvalidIssuer));
  const auto invalid = Ndmspc::NOidcHttpAuthenticator::Authenticate(verifier, "Bearer t");
  EXPECT_EQ(invalid.status, Ndmspc::NHttpAuthResult::Status::Invalid);
  EXPECT_EQ(invalid.errorCode, "invalid_issuer");
}

TEST(NOidcHttpAuthenticatorTest, ApplyToRequestRecordsIdentityOnSuccess)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Identity("subject-1", "alice"));
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetRequestHeader("Authorization: Bearer access-token\r\n");
  ASSERT_TRUE(Ndmspc::NOidcHttpAuthenticator::ApplyToRequest(verifier, arg.get()));
  EXPECT_STREQ(arg->GetUserName(), "alice");
  EXPECT_FALSE(arg->Is404());
  const std::string body(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
  EXPECT_TRUE(body.empty()); // no error body on success
}

TEST(NOidcHttpAuthenticatorTest, ApplyToRequestRejectsAndWritesErrorBody)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Failure(Ndmspc::NOidcErrorCode::ExpiredToken));
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetRequestHeader("Authorization: Bearer expired-token\r\n");
  ASSERT_FALSE(Ndmspc::NOidcHttpAuthenticator::ApplyToRequest(verifier, arg.get()));
  EXPECT_FALSE(arg->Is404());
  EXPECT_STREQ(arg->GetContentType(), "application/json");
  const std::string body(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
  EXPECT_NE(body.find("token_expired"), std::string::npos);
  // Ensure the caller can detect rejection without relying on the body alone.
  EXPECT_NE(std::string(arg->GetHeader("WWW-Authenticate")), "");
}

} // namespace
