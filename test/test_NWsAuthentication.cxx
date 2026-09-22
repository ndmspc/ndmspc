#include <gtest/gtest.h>
#include <THttpCallArg.h>
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NWsHandler.h"

#include <deque>
#include <memory>
#include <vector>

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

class TestWsHandler : public Ndmspc::NWsHandler {
  public:
  explicit TestWsHandler(std::shared_ptr<Ndmspc::IOidcTokenVerifier> verifier = nullptr)
      : NWsHandler("test", "test", std::move(verifier)) {}

  bool Pending(ULong_t wsId) const
  {
    std::lock_guard lock(fMutex);
    return fPendingClients.contains(wsId);
  }

  std::string Subject(ULong_t wsId) const
  {
    std::lock_guard lock(fMutex);
    return fClients.at(wsId).GetSubject();
  }

  std::string Username(ULong_t wsId) const
  {
    std::lock_guard lock(fMutex);
    return fClients.at(wsId).GetUsername();
  }
};

std::unique_ptr<THttpCallArg> Event(const char * method, ULong_t wsId, std::string payload = {},
                                   const char * query = nullptr)
{
  auto event = std::make_unique<THttpCallArg>();
  event->SetMethod(method);
  event->SetWSId(wsId);
  if (!payload.empty()) event->SetPostData(std::move(payload));
  if (query != nullptr) event->SetQuery(query);
  return event;
}

// What the websocket-connect filter was asked about, and the two answers it can give. A plain
// function (not a lambda) because the filter is a function pointer: the room router sets it from a
// macro, which cannot capture anything either.
std::vector<std::string> fFilterQueries;
Bool_t                   RecordAndAccept(const std::string & query)
{
  fFilterQueries.push_back(query);
  return kTRUE;
}
Bool_t RecordAndRefuse(const std::string & query)
{
  fFilterQueries.push_back(query);
  return kFALSE;
}

/// @brief Sets the process-wide filter for one test and clears it again, whatever the outcome.
struct WsFilterGuard {
  explicit WsFilterGuard(Ndmspc::NdmspcWsConnectFilter filter) { Ndmspc::gNdmspcWsConnectFilter = filter; }
  ~WsFilterGuard() { Ndmspc::gNdmspcWsConnectFilter = nullptr; }
};

Ndmspc::NOidcResult Identity(std::string subject, std::string username, std::string email = "")
{
  return {.identity = Ndmspc::NOidcIdentity{.subject = std::move(subject),
                                            .preferredUsername = std::move(username),
                                            .email = std::move(email),
                                            .expiresAt = std::chrono::system_clock::now() + std::chrono::minutes(5)},
          .error = Ndmspc::NOidcErrorCode::None, .diagnostic = {}};
}

TEST(NWsAuthenticationTest, AnonymousModeActivatesOnReady)
{
  TestWsHandler handler;
  auto ready = Event("WS_READY", 1);
  EXPECT_TRUE(handler.ProcessWS(ready.get()));
  EXPECT_EQ(handler.GetClientCount(), 1);
}

TEST(NWsAuthenticationTest, AuthenticatedModeKeepsReadyClientPending)
{
  auto verifier = std::make_shared<FakeVerifier>();
  TestWsHandler handler(verifier);
  auto ready = Event("WS_READY", 2);
  EXPECT_TRUE(handler.ProcessWS(ready.get()));
  EXPECT_TRUE(handler.Pending(2));
  EXPECT_EQ(handler.GetClientCount(), 0);
}

TEST(NWsAuthenticationTest, ValidFirstMessageBindsIdentity)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Identity("subject-1", "alice"));
  TestWsHandler handler(verifier);
  auto ready = Event("WS_READY", 3);
  handler.ProcessWS(ready.get());
  auto auth = Event("WS_DATA", 3, R"({"event":"authenticate","token":"access-token"})");
  EXPECT_TRUE(handler.ProcessWS(auth.get()));
  EXPECT_FALSE(handler.Pending(3));
  EXPECT_EQ(handler.GetClientCount(), 1);
  EXPECT_EQ(handler.Subject(3), "subject-1");
  EXPECT_EQ(handler.Username(3), "alice");
  ASSERT_EQ(verifier->tokens.size(), 1);
  EXPECT_EQ(verifier->tokens.front(), "access-token");
}

TEST(NWsAuthenticationTest, InvalidFirstMessageNeverActivatesClient)
{
  auto verifier = std::make_shared<FakeVerifier>();
  TestWsHandler handler(verifier);
  auto ready = Event("WS_READY", 4);
  handler.ProcessWS(ready.get());
  auto data = Event("WS_DATA", 4, R"({"path":"state"})");
  EXPECT_TRUE(handler.ProcessWS(data.get()));
  EXPECT_EQ(handler.GetClientCount(), 0);
  EXPECT_FALSE(handler.Pending(4));
  EXPECT_TRUE(verifier->tokens.empty());
}

TEST(NWsConnectFilterTest, RefusedUpgradeIsRejected)
{
  TestWsHandler   handler;
  WsFilterGuard   guard(RecordAndRefuse);
  fFilterQueries.clear();

  auto connect = Event("WS_CONNECT", 11, {}, "room=nosuch");
  EXPECT_FALSE(handler.ProcessWS(connect.get()));
  ASSERT_EQ(fFilterQueries.size(), 1u);
  EXPECT_EQ(fFilterQueries.front(), "room=nosuch");
}

TEST(NWsConnectFilterTest, AConnectionWithoutQueryIsAskedAboutToo)
{
  TestWsHandler handler;
  WsFilterGuard guard(RecordAndRefuse);
  fFilterQueries.clear();

  auto connect = Event("WS_CONNECT", 12);
  EXPECT_FALSE(handler.ProcessWS(connect.get()));
  ASSERT_EQ(fFilterQueries.size(), 1u);
  EXPECT_EQ(fFilterQueries.front(), "");
}

TEST(NWsConnectFilterTest, AcceptedUpgradeIsServed)
{
  TestWsHandler handler;
  WsFilterGuard guard(RecordAndAccept);

  auto connect = Event("WS_CONNECT", 13, {}, "room=known");
  EXPECT_TRUE(handler.ProcessWS(connect.get()));
}

TEST(NWsConnectFilterTest, WithoutAFilterEveryUpgradeIsAccepted)
{
  TestWsHandler handler;
  Ndmspc::gNdmspcWsConnectFilter = nullptr;

  EXPECT_TRUE(handler.ProcessWS(Event("WS_CONNECT", 14).get()));
  EXPECT_TRUE(handler.ProcessWS(Event("WS_CONNECT", 15, {}, "room=anything").get()));
}

TEST(NWsAuthenticationTest, RefreshKeepsSubjectAndUpdatesUsername)
{
  auto verifier = std::make_shared<FakeVerifier>();
  verifier->results.push_back(Identity("subject-1", "alice"));
  verifier->results.push_back(Identity("subject-1", "alice-new"));
  TestWsHandler handler(verifier);
  auto ready = Event("WS_READY", 5);
  handler.ProcessWS(ready.get());
  auto first = Event("WS_DATA", 5, R"({"event":"authenticate","token":"first"})");
  handler.ProcessWS(first.get());
  auto refresh = Event("WS_DATA", 5, R"({"event":"authenticate","token":"refresh"})");
  EXPECT_TRUE(handler.ProcessWS(refresh.get()));
  EXPECT_EQ(handler.Subject(5), "subject-1");
  EXPECT_EQ(handler.Username(5), "alice-new");
}

} // namespace
