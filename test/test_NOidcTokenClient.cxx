#include <gtest/gtest.h>

#include "ndmspc/http/NOidcTokenClient.h"
#include "ndmspc/http/NWsClient.h"

#include <deque>
#include <string>

namespace {

class FakeOidcHttpClient : public Ndmspc::IOidcTokenHttpClient {
  public:
  Ndmspc::NOidcTokenHttpResult Post(const std::string & url, const std::string & body,
                                    const std::string & contentType) override
  {
    lastUrl = url;
    lastBody = body;
    lastContentType = contentType;
    Ndmspc::NOidcTokenHttpResult result;
    if (!responses.empty()) {
      result = responses.front();
      responses.pop_front();
    }
    return result;
  }

  std::string lastUrl;
  std::string lastBody;
  std::string lastContentType;
  std::deque<Ndmspc::NOidcTokenHttpResult> responses;
};

class NOidcTokenTest : public ::testing::Test {
  protected:
  Ndmspc::NOidcTokenClientConfig cfg;
  std::shared_ptr<FakeOidcHttpClient> http = std::make_shared<FakeOidcHttpClient>();

  void SetUp() override
  {
    cfg.issuer = "https://keycloak.example/realms/ndmspc";
    cfg.clientId = "ndmspc-ui";
  }

  Ndmspc::NOidcTokenClient MakeClient() { return Ndmspc::NOidcTokenClient(cfg, http); }
};

TEST_F(NOidcTokenTest, ClientCredentialsGrantPostsCorrectBodyAndParsesToken)
{
  http->responses.push_back({200, R"({"access_token":"abc.def.ghi","expires_in":300,"token_type":"Bearer"})"});
  Ndmspc::NOidcTokenClient client = MakeClient();

  const std::string token = client.ObtainAccessToken();
  EXPECT_EQ(token, "abc.def.ghi");
  EXPECT_EQ(http->lastUrl, "https://keycloak.example/realms/ndmspc/protocol/openid-connect/token");
  EXPECT_EQ(http->lastContentType, "application/x-www-form-urlencoded");
  EXPECT_NE(http->lastBody.find("grant_type=client_credentials"), std::string::npos);
  EXPECT_NE(http->lastBody.find("client_id=ndmspc-ui"), std::string::npos);
  EXPECT_EQ(http->lastBody.find("username="), std::string::npos);
}

TEST_F(NOidcTokenTest, ClientCredentialsGrantIncludesSecret)
{
  cfg.clientSecret = "topsecret";
  http->responses.push_back({200, R"({"access_token":"tok"})"});
  Ndmspc::NOidcTokenClient client = MakeClient();

  ASSERT_EQ(client.ObtainAccessToken(), "tok");
  EXPECT_NE(http->lastBody.find("client_secret=topsecret"), std::string::npos);
}

TEST_F(NOidcTokenTest, PasswordGrantIncludesResourceOwnerCredentials)
{
  cfg.grant = Ndmspc::NOidcTokenClientConfig::Grant::Password;
  cfg.username = "alice";
  cfg.password = "secret";
  http->responses.push_back({200, R"({"access_token":"pw-token"})"});
  Ndmspc::NOidcTokenClient client = MakeClient();

  ASSERT_EQ(client.ObtainAccessToken(), "pw-token");
  EXPECT_NE(http->lastBody.find("grant_type=password"), std::string::npos);
  EXPECT_NE(http->lastBody.find("username=alice"), std::string::npos);
  EXPECT_NE(http->lastBody.find("password=secret"), std::string::npos);
}

TEST_F(NOidcTokenTest, ThrowsOnHttpErrorResponse)
{
  http->responses.push_back({400, R"({"error":"invalid_grant"})"});
  Ndmspc::NOidcTokenClient client = MakeClient();
  EXPECT_THROW(client.ObtainAccessToken(), std::runtime_error);
}

TEST_F(NOidcTokenTest, ThrowsWhenAccessTokenMissing)
{
  http->responses.push_back({200, R"({"refresh_token":"abc"})"});
  Ndmspc::NOidcTokenClient client = MakeClient();
  EXPECT_THROW(client.ObtainAccessToken(), std::runtime_error);
}

TEST_F(NOidcTokenTest, RejectsPasswordGrantWithoutCredentials)
{
  cfg.grant = Ndmspc::NOidcTokenClientConfig::Grant::Password;
  EXPECT_THROW(cfg.Validate(), std::invalid_argument);
}

TEST(NOidcTokenConfigTest, RequiresIssuerAndClientId)
{
  Ndmspc::NOidcTokenClientConfig c;
  c.clientId = "foo";
  EXPECT_THROW(c.Validate(), std::invalid_argument); // missing issuer
  c.issuer = "https://issuer.example/realms/r";
  EXPECT_NO_THROW(c.Validate());
}

} // namespace