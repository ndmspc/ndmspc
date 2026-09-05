#include <gtest/gtest.h>
#include <jwt-cpp/jwt.h>
#include "ndmspc/http/NOidcAuthenticator.h"

#include <deque>
#include <memory>

namespace {

constexpr const char * kPrivateKey = R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDcamdEG0OcmeCU
FXliEKECkKExzoV9d4JAkOG7CBmgz5vJTrxnmMReOPfnL5iaZPNGZB9yWfBy69eX
9ozHhSbMbVkPUucaHYfvkWHYfQZlrZW2khjjci3NB+IjEj3/vnSIC+95QLyDHwOQ
Kce54fJRTm6mAux2rVhMcExqLdnCGVerzMtEdJNlt6433mL++vgOsCgHVDUC8ilz
d0670n5WwJG5uYn1xpqIUUYEKCdbIMaYkD3f3NxsL6ggnxYo6DIjPGN+EJq5q7UT
sX4Au12pXe9jcq9OTMzz4PWLW6bA4Cep0qYpHHZNVV8vPTI9wXrzW8wGuMytxyCW
K20CD1f3AgMBAAECggEAUdjxH8lAAhbh3htbR58FKv6p3OTjjQOjynYCXIFVgvGU
19v0+kMwKAzfgWmbMTnrXGgxhTUApKwPEs7q1+wJzD+OorIWPwxYPg2uV8WVaoxa
28DEnTD48PnMb1mGzEDc5OgJtOzlE4ugtxfMoqnUYXzOebb8N9WGxFuvBH6iLgCq
rfOEd5PSCOumxM2tcV92AwOS7CTbbFX6o25DMVUQUm39DD5byJw3Y34W+Sf2W8k1
xph5D2d1abz8/2RZWgmhmR9mICttxmRvzO8KfuDerrTRm9EvZDl1CaEVRhGqbQ7h
+3P1BlZQ4A4bK0DqQgQW+BBh0rxUexEVR5I3HefxAQKBgQDxDbtJK+lXo7rZ2IG2
zfXOZ06c7aYEZGfLIK1MgrG/hMJTNkwiutExk8/Dk2RsSFcM4YBIrEtB3g+zxgnP
ba13HWXmfE05sF5xcE4ZtkScUD366ZW8AM9ZjCfLPO4Ybl6qnECW+1aul0v/g070
XlWHZ5Df99u4y18O6i0EonZ0rwKBgQDqFRUvgdGngroulQGX1f8xtOnP8TRA7s9N
ZLRlMaKp3viG8o98XTW0XHbLv2BuyhUcrwB6FW+N61fuVS7aBRYVto2eKya0KZgY
33y6fNZhlYLrC0D6JYHl0t9iHDLPeMhbc1X3rOVYukFNkF5yWs9/HaPwO6dPjoeK
F0wDN7ezOQKBgCkrWDZKCqNOMmZsZNMM4BNtb2674+PSJiv6G776f1MfYHUHy/8O
exYFkbFsZfVccYmgpeFDk+LfAz2H8Dr+F2dFnRa9Wg8lQSwMqzoW+CbeSYemB03B
sagwmMdMU7nWd9KZtypSKN7OtksgaQaxadgjZwnpchxgl46bji7BdIu/AoGAX2tm
rm3x46HDeVeeRaGjHEUOBojhbxKqCHdjndiE4VAV6RSZbu2kBbinaFjD24We44lm
3V09kxF7T5kDtzXZkdJPmkkmxswpxwHbGz3mOfMzYdK9kvqVH/U8wAaUo8QtkDHM
umCNQQTzt8WA6oagDMYtXLFEe4azM5RZlPoydOkCgYEA5eDB1TBF/CusgByMmjEO
aywAZKCrpsaS+EgUQOIk8+O3bBNKnjEMQkQW651VknXEg9ccHFWxoYko+sp1tNNS
8/JT8MdspYsDyp47rnS1uWIgnchP4aoblu/7UFsad6VdOXgb8UZx6Ogcu8oDihMv
ANmlj30lkbV5NrU/2qC5B+8=
-----END PRIVATE KEY-----)";
constexpr const char * kModulus = "3GpnRBtDnJnglBV5YhChApChMc6FfXeCQJDhuwgZoM-byU68Z5jEXjj35y-YmmTzRmQfclnwcuvXl_aMx4UmzG1ZD1LnGh2H75Fh2H0GZa2VtpIY43ItzQfiIxI9_750iAvveUC8gx8DkCnHueHyUU5upgLsdq1YTHBMai3ZwhlXq8zLRHSTZbeuN95i_vr4DrAoB1Q1AvIpc3dOu9J-VsCRubmJ9caaiFFGBCgnWyDGmJA939zcbC-oIJ8WKOgyIzxjfhCauau1E7F-ALtdqV3vY3KvTkzM8-D1i1umwOAnqdKmKRx2TVVfLz0yPcF681vMBrjMrccglittAg9X9w";

class FakeHttpClient : public Ndmspc::IOidcHttpClient {
  public:
  Ndmspc::NOidcHttpResult Get(const std::string &) override
  {
    if (responses.empty()) throw std::runtime_error("No fake response");
    auto result = responses.front();
    responses.pop_front();
    return result;
  }
  std::deque<Ndmspc::NOidcHttpResult> responses;
};

class OidcAuthenticatorTest : public ::testing::Test {
  protected:
  const std::chrono::system_clock::time_point now{std::chrono::seconds(1700000000)};
  Ndmspc::NOidcConfig config;
  std::shared_ptr<FakeHttpClient> http = std::make_shared<FakeHttpClient>();

  void SetUp() override
  {
    config.issuer = "https://keycloak.example/realms/test";
    config.audience = "ndmspc";
    config.jwksRefresh = std::chrono::hours(1);
    http->responses.push_back({200, R"({"issuer":"https://keycloak.example/realms/test","jwks_uri":"https://keycloak.example/realms/test/protocol/openid-connect/certs"})"});
    http->responses.push_back({200, std::string(R"({"keys":[{"kid":"test-key","kty":"RSA","alg":"RS256","use":"sig","n":")") + kModulus + R"(","e":"AQAB"}]})"});
  }

  std::string Token(std::string issuer = "https://keycloak.example/realms/test", std::string audience = "ndmspc",
                    std::chrono::seconds lifetime = std::chrono::minutes(5), std::string keyId = "test-key") const
  {
    return jwt::create()
        .set_key_id(std::move(keyId))
        .set_issuer(std::move(issuer))
        .set_audience(std::move(audience))
        .set_subject("subject-1")
        .set_payload_claim("preferred_username", jwt::claim(std::string("alice")))
        .set_issued_at(now)
        .set_expires_at(now + lifetime)
        .sign(jwt::algorithm::rs256("", kPrivateKey, "", ""));
  }
};

TEST(NOidcConfigTest, RequiresIssuerAndAudienceTogether)
{
  Ndmspc::NOidcConfig config;
  config.issuer = "https://keycloak.example/realms/test";
  EXPECT_THROW(config.Validate(), std::invalid_argument);
}

TEST_F(OidcAuthenticatorTest, VerifiesIdentity)
{
  Ndmspc::NKeycloakOidcAuthenticator authenticator(config, http, [this] { return now; });
  authenticator.Initialize();
  const auto result = authenticator.Verify(Token());
  ASSERT_TRUE(result);
  EXPECT_EQ(result.identity->subject, "subject-1");
  EXPECT_EQ(result.identity->preferredUsername, "alice");
}

TEST_F(OidcAuthenticatorTest, RejectsWrongIssuerAndAudience)
{
  Ndmspc::NKeycloakOidcAuthenticator authenticator(config, http, [this] { return now; });
  authenticator.Initialize();
  EXPECT_EQ(authenticator.Verify(Token("https://other.example/realms/test")).error, Ndmspc::NOidcErrorCode::InvalidIssuer);
  EXPECT_EQ(authenticator.Verify(Token(config.issuer, "other-client")).error, Ndmspc::NOidcErrorCode::InvalidAudience);
}

TEST_F(OidcAuthenticatorTest, RejectsExpiredToken)
{
  Ndmspc::NKeycloakOidcAuthenticator authenticator(config, http, [this] { return now; });
  authenticator.Initialize();
  EXPECT_EQ(authenticator.Verify(Token(config.issuer, config.audience, std::chrono::seconds(-60))).error,
            Ndmspc::NOidcErrorCode::ExpiredToken);
}

TEST_F(OidcAuthenticatorTest, RejectsUnknownKey)
{
  http->responses.push_back({200, std::string(R"({"keys":[{"kid":"test-key","kty":"RSA","alg":"RS256","use":"sig","n":")") + kModulus + R"(","e":"AQAB"}]})"});
  Ndmspc::NKeycloakOidcAuthenticator authenticator(config, http, [this] { return now; });
  authenticator.Initialize();
  EXPECT_EQ(authenticator.Verify(Token(config.issuer, config.audience, std::chrono::minutes(5), "unknown")).error,
            Ndmspc::NOidcErrorCode::UnknownKey);
}

} // namespace
