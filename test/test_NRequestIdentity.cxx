#include <gtest/gtest.h>

#include <THttpCallArg.h>

#include <memory>

#include "ndmspc/http/NRequestIdentity.h"

using Ndmspc::NRequestIdentity;

TEST(NRequestIdentityTest, EmptyIdentityKnowsNothing)
{
  const NRequestIdentity identity;
  EXPECT_TRUE(identity.Empty());
  EXPECT_EQ(identity.Owner(), "");
  EXPECT_TRUE(identity.Identifiers().empty());
  EXPECT_FALSE(identity.verified);
}

TEST(NRequestIdentityTest, OwnerPrefersTheEmailThenTheUserThenTheSubject)
{
  NRequestIdentity identity;
  identity.subject = "subject-1";
  EXPECT_EQ(identity.Owner(), "subject-1");

  identity.username = "alice";
  EXPECT_EQ(identity.Owner(), "alice");

  identity.email = "alice@example.com";
  EXPECT_EQ(identity.Owner(), "alice@example.com");
}

TEST(NRequestIdentityTest, MatchesAnyIdentifierCaseInsensitively)
{
  NRequestIdentity identity;
  identity.subject  = "11111111-2222-3333-4444-555555555555";
  identity.username = "alice";
  identity.email    = "Alice@Example.com";

  EXPECT_TRUE(identity.Matches("alice@example.com"));
  EXPECT_TRUE(identity.Matches("ALICE"));
  EXPECT_TRUE(identity.Matches("  11111111-2222-3333-4444-555555555555 "));
  EXPECT_FALSE(identity.Matches("bob"));

  // An empty candidate matches nobody: "who is the owner of this room" is never answered by "nobody
  // said", which is what would make an unowned room look like everyone's.
  EXPECT_FALSE(identity.Matches(""));
}

TEST(NRequestIdentityTest, JsonRoundTripKeepsWhatWasVerified)
{
  NRequestIdentity identity;
  identity.subject  = "subject-1";
  identity.username = "alice";
  identity.email    = "alice@example.com";
  identity.verified = true;

  const auto restored = NRequestIdentity::FromJson(identity.ToJson());
  EXPECT_EQ(restored.subject, "subject-1");
  EXPECT_EQ(restored.username, "alice");
  EXPECT_EQ(restored.email, "alice@example.com");
  EXPECT_TRUE(restored.verified);
  EXPECT_FALSE(restored.Empty());
}

TEST(NRequestIdentityTest, MalformedJsonYieldsNothingRatherThanThrowing)
{
  EXPECT_TRUE(NRequestIdentity::FromJson(json(nullptr)).Empty());
  EXPECT_TRUE(NRequestIdentity::FromJson(json::array({1, 2})).Empty());
  EXPECT_TRUE(NRequestIdentity::FromJson(json{{"user", 42}, {"email", false}}).Empty());
  // Fields of the wrong type are ignored, and an unverified identity stays unverified.
  const auto mixed = NRequestIdentity::FromJson(json{{"user", "alice"}, {"verified", "yes"}});
  EXPECT_EQ(mixed.username, "alice");
  EXPECT_FALSE(mixed.verified);
}

TEST(NRequestIdentityTest, AnAssertionIsNeverVerified)
{
  const auto asserted = NRequestIdentity::FromAssertion("alice@example.com");
  EXPECT_FALSE(asserted.verified);
  EXPECT_EQ(asserted.Owner(), "alice@example.com");
  EXPECT_TRUE(asserted.Matches("alice@example.com"));

  // A user name only appears on an argument that something authenticated.
  const auto authenticated = NRequestIdentity::FromUsername("alice");
  EXPECT_TRUE(authenticated.verified);
  EXPECT_TRUE(authenticated.Empty() == false);
}

TEST(NRequestIdentityTest, ForwardedHeadersCarryTheFrontDoorsVerification)
{
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetRequestHeader("X-NDMSPC-User: alice\r\nX-NDMSPC-Subject: CN=alice\r\n"
                        "X-NDMSPC-Email: alice@example.com\r\n");

  const auto identity = NRequestIdentity::FromForwardedHeaders(arg.get());
  EXPECT_EQ(identity.username, "alice");
  EXPECT_EQ(identity.subject, "CN=alice");
  EXPECT_EQ(identity.email, "alice@example.com");
  EXPECT_TRUE(identity.verified);

  auto none = std::make_shared<THttpCallArg>();
  EXPECT_TRUE(NRequestIdentity::FromForwardedHeaders(none.get()).Empty());
  EXPECT_TRUE(NRequestIdentity::FromForwardedHeaders(nullptr).Empty());
}

TEST(NRequestIdentityTest, ASessionIsVerifiedAndCarriesTheEmail)
{
  Ndmspc::NOidcSession session;
  session.subject  = "subject-1";
  session.username = "alice";
  session.email    = "alice@example.com";

  const auto identity = NRequestIdentity::FromSession(session);
  EXPECT_TRUE(identity.verified);
  EXPECT_EQ(identity.Owner(), "alice@example.com");
}
