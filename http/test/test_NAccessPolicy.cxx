#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "ndmspc/http/NAccessPolicy.h"
#include "ndmspc/http/NRequestIdentity.h"

namespace {

using Ndmspc::NAccessPolicy;
using Ndmspc::NRequestIdentity;

/// @brief Sets an environment variable for one test and restores it afterwards.
class EnvGuard {
  public:
  EnvGuard(const char * name, const char * value) : fName(name)
  {
    if (const char * old = std::getenv(name); old != nullptr) {
      fHad = true;
      fOld = old;
    }
    if (value != nullptr) ::setenv(name, value, 1);
    else ::unsetenv(name);
  }
  ~EnvGuard()
  {
    if (fHad) ::setenv(fName.c_str(), fOld.c_str(), 1);
    else ::unsetenv(fName.c_str());
  }

  private:
  std::string fName;
  std::string fOld;
  bool        fHad{false};
};

/// @brief A verified user known by a user name, an email and a subject.
NRequestIdentity User()
{
  NRequestIdentity identity;
  identity.subject  = "sub-1";
  identity.username = "alice";
  identity.email    = "Alice@Example.com";
  identity.verified = true;
  return identity;
}

} // namespace

TEST(NAccessPolicyTest, AnEmptyListAllowsEveryone)
{
  const NAccessPolicy policy;
  EXPECT_FALSE(policy.Configured());
  // Even a request with no identity at all: with no list, the deployment lets everyone in.
  EXPECT_TRUE(policy.Allows(NRequestIdentity{}));
  EXPECT_TRUE(policy.Allows(User()));
  EXPECT_EQ(policy.Message(), std::string(NAccessPolicy::DefaultMessage()));
}

TEST(NAccessPolicyTest, AListedUserMatchesAnyOfItsIdentifiers)
{
  // Whichever form the admin happened to write: user name, email or subject - case-insensitively.
  EXPECT_TRUE(NAccessPolicy({"alice"}, "").Allows(User()));
  EXPECT_TRUE(NAccessPolicy({"ALICE"}, "").Allows(User()));
  EXPECT_TRUE(NAccessPolicy({" alice@example.com "}, "").Allows(User()));
  EXPECT_TRUE(NAccessPolicy({"sub-1"}, "").Allows(User()));
  // Any one entry is enough.
  EXPECT_TRUE(NAccessPolicy({"bob", "alice"}, "").Allows(User()));
}

TEST(NAccessPolicyTest, AStrangerIsRefused)
{
  const NAccessPolicy policy({"bob"}, "ask an admin");
  EXPECT_TRUE(policy.Configured());
  EXPECT_FALSE(policy.Allows(User()));
  EXPECT_EQ(policy.Message(), "ask an admin");

  // An identity that knows nothing is refused too, on a list that names only people.
  EXPECT_FALSE(policy.Allows(NRequestIdentity{}));
}

TEST(NAccessPolicyTest, TheMessageFallsBackToTheDefault)
{
  EXPECT_EQ(NAccessPolicy({"alice"}, "").Message(), std::string(NAccessPolicy::DefaultMessage()));
}

TEST(NAccessPolicyTest, FromEnvironmentSplitsTrimsAndDropsEmptyEntries)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", " alice , ,bob@example.com,");
  EnvGuard message("NDMSPC_ACCESS_DENIED_MESSAGE", "ask an admin");

  const NAccessPolicy policy = NAccessPolicy::FromEnvironment();
  EXPECT_TRUE(policy.Configured());
  ASSERT_EQ(policy.Allowed().size(), 2u);
  EXPECT_EQ(policy.Allowed()[0], "alice");
  EXPECT_EQ(policy.Allowed()[1], "bob@example.com");
  EXPECT_EQ(policy.Message(), "ask an admin");
}

TEST(NAccessPolicyTest, RoleIsOwnerForListedAndGuestForOthers)
{
  const NAccessPolicy policy({"alice"}, "nope");
  EXPECT_EQ(policy.RoleOf(User()), "owner");
  EXPECT_FALSE(policy.IsGuest(User()));

  NRequestIdentity bob;
  bob.username = "bob";
  bob.verified = true;
  EXPECT_EQ(policy.RoleOf(bob), "guest");
  EXPECT_TRUE(policy.IsGuest(bob));

  // With no list, everyone is an owner.
  const NAccessPolicy open;
  EXPECT_EQ(open.RoleOf(bob), "owner");
  EXPECT_FALSE(open.IsGuest(bob));
}

TEST(NAccessPolicyTest, TheModeDefaultsToGuestAndDenyIsOptIn)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", "alice");

  EnvGuard unset("NDMSPC_ALLOWED_USERS_MODE", nullptr);
  EXPECT_FALSE(NAccessPolicy::FromEnvironment().IsDeny());
  EXPECT_EQ(NAccessPolicy::FromEnvironment().GetMode(), NAccessPolicy::Mode::Guest);

  EnvGuard deny("NDMSPC_ALLOWED_USERS_MODE", "deny");
  EXPECT_TRUE(NAccessPolicy::FromEnvironment().IsDeny());
}

TEST(NAccessPolicyTest, FromEnvironmentIsOpenWhenTheListIsUnset)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", nullptr);
  EnvGuard message("NDMSPC_ACCESS_DENIED_MESSAGE", nullptr);

  const NAccessPolicy policy = NAccessPolicy::FromEnvironment();
  EXPECT_FALSE(policy.Configured());
  EXPECT_TRUE(policy.Allows(User()));
  EXPECT_EQ(policy.Message(), std::string(NAccessPolicy::DefaultMessage()));
}
