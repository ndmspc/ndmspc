// Unit tests for NRoomSession: what it captures from a room, what it refuses to capture,
// and the exact sequence it replays. The HTTP transport is faked, so no room or cluster
// is needed (the same approach test_NOidcTokenClient.cxx takes).
#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ndmspc/http/NRoomSession.h"

namespace {

constexpr const char * kBase = "http://room.test:80";

/// @brief A transport that records every request and replays canned responses.
class FakeHttpRequest : public Ndmspc::NHttpRequest {
  public:
  struct Call {
    std::string method;
    std::string url;
    std::string body;
  };

  std::vector<Call>                                  calls;
  std::map<std::string, std::pair<int, std::string>> responses; ///< "METHOD url" -> {status, body}
  bool                                               throwOnRequest{false};

  Ndmspc::NHttpResponse request(const std::string & method, const std::string & url, const std::string & body,
                                const std::map<std::string, std::string> & /*headers*/,
                                const std::string & /*cert*/, const std::string & /*key*/,
                                const std::string & /*keyPassword*/, const std::string & /*caFile*/,
                                const std::string & /*caPath*/, bool /*insecure*/) override
  {
    calls.push_back({method, url, body});
    if (throwOnRequest) throw std::runtime_error("Connection refused");

    Ndmspc::NHttpResponse response;
    const auto            it = responses.find(method + " " + url);
    if (it == responses.end()) {
      response.status = 404;
      response.body   = R"({"error":"not found"})";
      return response;
    }
    response.status = it->second.first;
    response.body   = it->second.second;
    return response;
  }

  void Respond(const std::string & method, const std::string & url, const std::string & body, int status = 200)
  {
    responses[method + " " + url] = {status, body};
  }

  /// @brief "METHOD url" for every call, in order.
  std::vector<std::string> Sequence() const
  {
    std::vector<std::string> sequence;
    for (const auto & call : calls) sequence.push_back(call.method + " " + call.url);
    return sequence;
  }
};

// --- response builders, mirroring the room's real payloads -----------------------

std::string OpenSuccess(const std::string & file = "NBinnings01Gaus.root")
{
  return R"({"result":"success","file":")" + file + R"(","treename":"ngnt","nDimensions":3})";
}

std::string OpenEmpty() { return R"({"result":"failure","error":"File  not opened"})"; }

std::string RootWithHistory(const std::string & historyJson)
{
  return R"({"message":"Welcome","result":"success","state":{"history":)" + historyJson + "}}";
}

std::string StateWithPoint(const std::string & pointJson)
{
  return R"({"result":"success","payload":{"metadata":{"spectra":{"point":)" + pointJson + "}}}}";
}

std::string Ok() { return R"({"result":"success"})"; }

/// @brief Build a snapshot from (route, request body) pairs.
json SnapshotOf(const std::vector<std::pair<std::string, json>> & actions)
{
  json list = json::array();
  for (const auto & action : actions) {
    json entry;
    entry["name"] = action.first;
    entry["in"]   = action.second;
    list.push_back(std::move(entry));
  }
  json snapshot;
  snapshot["actions"] = list;
  return snapshot;
}

/// @brief A room with a file open, a reshape recorded, and a drill-down point.
void PrimeActiveRoom(FakeHttpRequest & fake, const std::string & point = "[0,1]")
{
  const std::string history =
      R"([{"name":"ngnt/open","method":"POST","payload":{"in":{"file":"NBinnings01Gaus.root"}}},)"
      R"({"name":"ngnt/reshape","method":"POST","payload":{"in":{"binningName":"b0","levels":[[0,1,2]]}}},)"
      R"({"name":"ngnt/map","method":"POST","payload":{"in":{"mappingPad":"pad3"}}}])";

  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", OpenSuccess());
  fake.Respond("GET", std::string(kBase) + "/api/", RootWithHistory(history));
  fake.Respond("GET", std::string(kBase) + "/api/state", StateWithPoint(point));
}

// --- which actions are worth replaying -------------------------------------------

TEST(NRoomSessionReplayTest, OnlyStateDefiningActionsAreReplayed)
{
  EXPECT_TRUE(Ndmspc::NRoomSession::IsReplayable("ngnt/open"));
  EXPECT_TRUE(Ndmspc::NRoomSession::IsReplayable("ngnt/reshape"));
  // These render or read; replaying them would recompute histograms for nothing.
  EXPECT_FALSE(Ndmspc::NRoomSession::IsReplayable("ngnt/map"));
  EXPECT_FALSE(Ndmspc::NRoomSession::IsReplayable("ngnt/spectra"));
  EXPECT_FALSE(Ndmspc::NRoomSession::IsReplayable("ngnt/point"));
  EXPECT_FALSE(Ndmspc::NRoomSession::IsReplayable("room/open"));
}

// --- probing a room --------------------------------------------------------------

TEST(NRoomSessionProbeTest, ReportsActiveWithTheOpenedFile)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);

  std::string file;
  std::string error;
  EXPECT_EQ(Ndmspc::NRoomSession::Probe(fake, kBase, file, error), Ndmspc::NRoomSession::State::Active);
  EXPECT_EQ(file, "NBinnings01Gaus.root");
  EXPECT_TRUE(error.empty());
}

TEST(NRoomSessionProbeTest, ReportsEmptyWhenNothingIsOpen)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", OpenEmpty());

  std::string file;
  std::string error;
  EXPECT_EQ(Ndmspc::NRoomSession::Probe(fake, kBase, file, error), Ndmspc::NRoomSession::State::Empty);
}

TEST(NRoomSessionProbeTest, ReportsUnreachableOnTransportFailure)
{
  FakeHttpRequest fake;
  fake.throwOnRequest = true;

  std::string file;
  std::string error;
  EXPECT_EQ(Ndmspc::NRoomSession::Probe(fake, kBase, file, error), Ndmspc::NRoomSession::State::Unreachable);
  EXPECT_NE(error.find("cannot reach"), std::string::npos);
}

TEST(NRoomSessionProbeTest, ReportsUnreachableOnAnErrorStatus)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", R"({"error":"boom"})", 500);

  std::string file;
  std::string error;
  EXPECT_EQ(Ndmspc::NRoomSession::Probe(fake, kBase, file, error), Ndmspc::NRoomSession::State::Unreachable);
  EXPECT_NE(error.find("HTTP 500"), std::string::npos);
}

TEST(NRoomSessionProbeTest, ReportsRefusedWhenTheRoomWantsACredentialItDidNotGet)
{
  // A room that authenticates /api answers the router - which is an internal component and has no
  // user token - with a refusal, HTTP 200 and the reason in the envelope (ROOT cannot set an error
  // status). That is not a room with nothing open, and saying so is what keeps a capture that
  // cannot happen from looking like an empty room.
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open",
               R"({"error":{"code":"authentication_required","message":"Missing Authorization header","retryable":false}})");

  std::string file;
  std::string error;
  EXPECT_EQ(Ndmspc::NRoomSession::Probe(fake, kBase, file, error), Ndmspc::NRoomSession::State::Refused);
  EXPECT_NE(error.find("authentication_required"), std::string::npos);
}

TEST(NRoomSessionProbeTest, ReportsRefusedWhenTheRoomsOwnGateTurnsTheTokenAway)
{
  // The room's own access gate uses the other envelope: a failure result with the code beside it.
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open",
               R"({"result":"failure","error":"this room does not accept that access token","code":"invalid_access_token"})");

  std::string file;
  std::string error;
  EXPECT_EQ(Ndmspc::NRoomSession::Probe(fake, kBase, file, error), Ndmspc::NRoomSession::State::Refused);
  EXPECT_NE(error.find("invalid_access_token"), std::string::npos);
}

// --- capturing -------------------------------------------------------------------

TEST(NRoomSessionCaptureTest, CapturesNothingFromAnEmptyRoom)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", OpenEmpty());
  fake.Respond("GET", std::string(kBase) + "/api/", RootWithHistory("[]"));

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);

  // The safety rule: a room with nothing open must never produce a snapshot, or it would
  // overwrite a good one on every wake.
  EXPECT_TRUE(snapshot.is_null()) << snapshot.dump();
  // ... and a room that is merely empty is not a refusal: nothing to report.
  EXPECT_TRUE(error.empty()) << error;
}

TEST(NRoomSessionCaptureTest, ReportsARefusedRoomRatherThanAnEmptyOne)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open",
               R"({"error":{"code":"authentication_required","message":"Missing Authorization header","retryable":false}})");

  std::string                 error;
  Ndmspc::NRoomSession::State reported = Ndmspc::NRoomSession::State::Empty;
  const json snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error, "", &reported);

  EXPECT_TRUE(snapshot.is_null());
  EXPECT_EQ(reported, Ndmspc::NRoomSession::State::Refused);
  EXPECT_FALSE(error.empty()) << "a refused capture must carry the reason";
}

TEST(NRoomSessionCaptureTest, CapturesTheFileTheReplayableActionsAndThePoint)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);

  ASSERT_TRUE(snapshot.is_object()) << error;
  EXPECT_EQ(snapshot["v"], 1);
  EXPECT_EQ(snapshot["room"], "sess1");
  EXPECT_EQ(snapshot["file"], "NBinnings01Gaus.root");
  EXPECT_GT(snapshot["at"].get<long>(), 0);

  ASSERT_TRUE(snapshot["actions"].is_array());
  ASSERT_EQ(snapshot["actions"].size(), 2u); // ngnt/map is excluded
  EXPECT_EQ(snapshot["actions"][0]["name"], "ngnt/open");
  EXPECT_EQ(snapshot["actions"][0]["in"]["file"], "NBinnings01Gaus.root");
  EXPECT_EQ(snapshot["actions"][1]["name"], "ngnt/reshape");
  EXPECT_EQ(snapshot["actions"][1]["in"]["binningName"], "b0");

  ASSERT_TRUE(snapshot.contains("point"));
  EXPECT_EQ(snapshot["point"], json::parse("[0,1]"));
}

TEST(NRoomSessionCaptureTest, KeepsTheServersQueryInjectionOutOfTheSnapshot)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", OpenSuccess());
  // A request made through the gateway arrives with the room parameter in its query
  // string, which the server folds into the body as "_query".
  fake.Respond("GET", std::string(kBase) + "/api/",
               RootWithHistory(R"([{"name":"ngnt/open","payload":{"in":{"_query":"room=test",)"
                               R"("file":"NBinnings01Gaus.root"}}}])"));
  fake.Respond("GET", std::string(kBase) + "/api/state", StateWithPoint("[]"));

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);

  ASSERT_TRUE(snapshot.is_object()) << error;
  ASSERT_EQ(snapshot["actions"].size(), 1u);
  EXPECT_FALSE(snapshot["actions"][0]["in"].contains("_query"));
  EXPECT_EQ(snapshot["actions"][0]["in"]["file"], "NBinnings01Gaus.root");
}

TEST(NRoomSessionCaptureTest, ReadsTheRoomsOwnEndpoints)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);

  std::string error;
  Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);

  const std::vector<std::string> expected = {
      std::string("GET ") + kBase + "/api/ngnt/open",
      std::string("GET ") + kBase + "/api/",
      std::string("GET ") + kBase + "/api/state",
  };
  EXPECT_EQ(fake.Sequence(), expected);
}

TEST(NRoomSessionCaptureTest, ReopensTheFileWhenTheHistoryIsEmpty)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", OpenSuccess());
  fake.Respond("GET", std::string(kBase) + "/api/", RootWithHistory("[]"));
  fake.Respond("GET", std::string(kBase) + "/api/state", Ok());

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);

  ASSERT_TRUE(snapshot.is_object()) << error;
  ASSERT_EQ(snapshot["actions"].size(), 1u);
  EXPECT_EQ(snapshot["actions"][0]["name"], "ngnt/open");
  EXPECT_EQ(snapshot["actions"][0]["in"]["file"], "NBinnings01Gaus.root");
}

TEST(NRoomSessionCaptureTest, KeepsTheSessionWhenTheStatePointIsUnavailable)
{
  FakeHttpRequest fake;
  fake.Respond("GET", std::string(kBase) + "/api/ngnt/open", OpenSuccess());
  fake.Respond("GET", std::string(kBase) + "/api/",
               RootWithHistory(R"([{"name":"ngnt/open","payload":{"in":{"file":"NBinnings01Gaus.root"}}}])"));
  fake.Respond("GET", std::string(kBase) + "/api/state", R"({"error":"boom"})", 500);

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);

  ASSERT_TRUE(snapshot.is_object()) << error;
  EXPECT_FALSE(snapshot.contains("point"));
  EXPECT_TRUE(error.empty()); // an optional step must not leave an error behind
}

TEST(NRoomSessionCaptureTest, CapturesNothingWhenTheRoomCannotBeReached)
{
  FakeHttpRequest fake;
  fake.throwOnRequest = true;

  std::string error;
  EXPECT_TRUE(Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error).is_null());
  EXPECT_FALSE(error.empty());
}

// --- restoring -------------------------------------------------------------------

TEST(NRoomSessionRestoreTest, ReplaysTheActionsInOrderThenThePoint)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/open", Ok());
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/reshape", Ok());
  fake.Respond("PATCH", std::string(kBase) + "/api/ngnt/map", Ok());

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);
  ASSERT_TRUE(snapshot.is_object()) << error;
  fake.calls.clear();

  ASSERT_TRUE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error)) << error;

  const std::vector<std::string> expected = {
      std::string("POST ") + kBase + "/api/ngnt/open",
      std::string("POST ") + kBase + "/api/ngnt/reshape",
      std::string("PATCH ") + kBase + "/api/ngnt/map",
      std::string("GET ") + kBase + "/api/state", // the restored point is read back
  };
  ASSERT_EQ(fake.Sequence(), expected);
  EXPECT_EQ(fake.calls[0].body, R"({"file":"NBinnings01Gaus.root"})");
  EXPECT_EQ(fake.calls[1].body, R"({"binningName":"b0","levels":[[0,1,2]]})");
  EXPECT_NE(fake.calls[2].body.find("\"point\":[0,1]"), std::string::npos) << fake.calls[2].body;
}

TEST(NRoomSessionRestoreTest, DoesNotPatchWhenThereIsNoPoint)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake, "[]");
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/open", Ok());
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/reshape", Ok());

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);
  ASSERT_TRUE(snapshot.is_object()) << error;
  fake.calls.clear();

  ASSERT_TRUE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error)) << error;
  EXPECT_EQ(fake.Sequence().size(), 2u);
}

TEST(NRoomSessionRestoreTest, AcceptsAPointWhosePatchRepliedWithAnError)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/open", Ok());
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/reshape", Ok());
  // What a freshly started room really answers: the point is stored, but rendering the
  // projection fails because nothing has been mapped yet.
  fake.Respond("PATCH", std::string(kBase) + "/api/ngnt/map",
               R"({"error":"No entry and no projection found, nothing sent to websocket","result":null})");

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);
  ASSERT_TRUE(snapshot.is_object()) << error;

  // The readback still reports the point, so the restore counts as successful.
  fake.Respond("GET", std::string(kBase) + "/api/state", StateWithPoint("[0,1]"));
  EXPECT_TRUE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error)) << error;
}

TEST(NRoomSessionRestoreTest, FailsWhenTheRoomDoesNotKeepThePoint)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/open", Ok());
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/reshape", Ok());
  fake.Respond("PATCH", std::string(kBase) + "/api/ngnt/map", Ok());

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);
  ASSERT_TRUE(snapshot.is_object()) << error;

  fake.Respond("GET", std::string(kBase) + "/api/state", StateWithPoint("[]"));
  EXPECT_FALSE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error));
  EXPECT_NE(error.find("did not keep the state point"), std::string::npos);
}

TEST(NRoomSessionRestoreTest, StopsAtTheFirstFailingStep)
{
  FakeHttpRequest fake;
  fake.Respond("POST", std::string(kBase) + "/api/ngnt/open", R"({"result":"failure","error":"cannot open file"})");

  const json snapshot =
      SnapshotOf({{"ngnt/open", {{"file", "missing.root"}}}, {"ngnt/reshape", {{"binningName", "b0"}}}});

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error));
  EXPECT_NE(error.find("cannot open file"), std::string::npos);
  EXPECT_EQ(fake.calls.size(), 1u); // the second action was not attempted
}

TEST(NRoomSessionRestoreTest, RefusesAnUnknownAction)
{
  FakeHttpRequest fake;

  const json snapshot = SnapshotOf({{"room/close", json::object()}});

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error));
  EXPECT_NE(error.find("cannot be replayed"), std::string::npos);
  EXPECT_TRUE(fake.calls.empty());
}

TEST(NRoomSessionRestoreTest, RefusesASnapshotWithoutActions)
{
  FakeHttpRequest fake;

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::Restore(fake, kBase, json::object(), error));
  EXPECT_FALSE(Ndmspc::NRoomSession::Restore(fake, kBase, json("not an object"), error));
}

TEST(NRoomSessionRestoreTest, ReportsATransportFailure)
{
  FakeHttpRequest fake;
  fake.throwOnRequest = true;

  const json snapshot = SnapshotOf({{"ngnt/open", {{"file", "f.root"}}}});

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::Restore(fake, kBase, snapshot, error));
  EXPECT_NE(error.find("cannot reach"), std::string::npos);
}

// --- encoding for storage --------------------------------------------------------

TEST(NRoomSessionEncodeTest, RoundTripsASnapshot)
{
  FakeHttpRequest fake;
  PrimeActiveRoom(fake);

  std::string error;
  const json  snapshot = Ndmspc::NRoomSession::Capture(fake, kBase, "sess1", error);
  const std::string text = Ndmspc::NRoomSession::Encode(snapshot);

  ASSERT_FALSE(text.empty());
  json decoded;
  ASSERT_TRUE(Ndmspc::NRoomSession::Decode(text, decoded));
  EXPECT_EQ(decoded, snapshot);
}

TEST(NRoomSessionEncodeTest, RefusesEmptyAndOversizedSnapshots)
{
  EXPECT_TRUE(Ndmspc::NRoomSession::Encode(json()).empty());
  EXPECT_TRUE(Ndmspc::NRoomSession::Encode(json::object()).empty());

  json big;
  big["file"] = std::string(Ndmspc::NRoomSession::kMaxEncodedBytes + 1, 'x');
  EXPECT_TRUE(Ndmspc::NRoomSession::Encode(big).empty());
}

TEST(NRoomSessionEncodeTest, RefusesTextThatIsNotASnapshot)
{
  json snapshot;
  EXPECT_FALSE(Ndmspc::NRoomSession::Decode("", snapshot));
  EXPECT_FALSE(Ndmspc::NRoomSession::Decode("not json at all", snapshot));
  EXPECT_FALSE(Ndmspc::NRoomSession::Decode("[1,2,3]", snapshot));
}

/// @brief A fake in-process dispatcher, as a room uses when it restores itself.
struct FakeRoom {
  std::vector<std::string>           calls;                     ///< "METHOD route", in order
  std::map<std::string, json>        replies;                   ///< "METHOD route" -> response
  std::map<std::string, std::string> failures;                  ///< "METHOD route" -> dispatch error

  Ndmspc::NRoomSession::Dispatch Dispatcher()
  {
    return [this](const std::string & method, const std::string & route, const json & /*body*/,
                  std::string & error) -> json {
      const std::string key = method + " " + route;
      calls.push_back(key);

      const auto failed = failures.find(key);
      if (failed != failures.end()) error = failed->second;

      const auto reply = replies.find(key);
      return reply != replies.end() ? reply->second : json();
    };
  }

  void Reply(const std::string & method, const std::string & route, const json & body)
  {
    replies[method + " " + route] = body;
  }

  void Fail(const std::string & method, const std::string & route, const std::string & error)
  {
    failures[method + " " + route] = error;
  }
};

// --- building a snapshot from a server's own state -------------------------------

TEST(NRoomSessionBuildTest, RefusesToBuildASnapshotWithoutAFile)
{
  // The empty-session rule: a room with nothing open must never produce a snapshot, or a
  // freshly started pod would overwrite a good one on every wake.
  EXPECT_TRUE(Ndmspc::NRoomSession::Build("sess1", "", json::array(), json()).is_null());
}

TEST(NRoomSessionBuildTest, KeepsOnlyTheReplayableActionsInOrder)
{
  const json history = json::parse(
      R"([{"name":"ngnt/open","payload":{"in":{"file":"f.root"}}},)"
      R"({"name":"ngnt/map","payload":{"in":{"mappingPad":"pad3"}}},)"
      R"({"name":"ngnt/reshape","payload":{"in":{"binningName":"b0"}}}])");

  const json snapshot = Ndmspc::NRoomSession::Build("sess1", "f.root", history, json());

  ASSERT_TRUE(snapshot.is_object());
  EXPECT_EQ(snapshot["room"], "sess1");
  EXPECT_EQ(snapshot["file"], "f.root");
  ASSERT_EQ(snapshot["actions"].size(), 2u);
  EXPECT_EQ(snapshot["actions"][0]["name"], "ngnt/open");
  EXPECT_EQ(snapshot["actions"][1]["name"], "ngnt/reshape");
}

TEST(NRoomSessionBuildTest, ReopensTheFileWhenThereIsNoHistory)
{
  const json snapshot = Ndmspc::NRoomSession::Build("sess1", "f.root", json::array(), json());

  ASSERT_TRUE(snapshot.is_object());
  ASSERT_EQ(snapshot["actions"].size(), 1u);
  EXPECT_EQ(snapshot["actions"][0]["name"], "ngnt/open");
  EXPECT_EQ(snapshot["actions"][0]["in"]["file"], "f.root");
}

TEST(NRoomSessionBuildTest, OmitsAnEmptyStatePoint)
{
  const json withoutPoint = Ndmspc::NRoomSession::Build("sess1", "f.root", json::array(), json::array());
  EXPECT_FALSE(withoutPoint.contains("point"));

  const json withPoint = Ndmspc::NRoomSession::Build("sess1", "f.root", json::array(), json::parse("[1]"));
  EXPECT_EQ(withPoint["point"], json::parse("[1]"));
}

// --- replaying through a dispatcher (the path a room takes) -----------------------

TEST(NRoomSessionRestoreInPlaceTest, ReplaysTheActionsAndVerifiesThePoint)
{
  FakeRoom room;
  room.Reply("POST", "ngnt/open", {{"result", "success"}});
  room.Reply("POST", "ngnt/reshape", {{"result", "success"}});
  room.Reply("PATCH", "ngnt/map", {{"result", "success"}});
  room.Reply("GET", "state", json::parse(StateWithPoint("[0,1]")));

  json snapshot = SnapshotOf({{"ngnt/open", {{"file", "f.root"}}}, {"ngnt/reshape", {{"binningName", "b0"}}}});
  snapshot["point"] = json::parse("[0,1]");

  std::string error;
  EXPECT_TRUE(Ndmspc::NRoomSession::RestoreInPlace(snapshot, room.Dispatcher(), error)) << error;

  const std::vector<std::string> expected = {"POST ngnt/open", "POST ngnt/reshape", "PATCH ngnt/map", "GET state"};
  EXPECT_EQ(room.calls, expected);
}

TEST(NRoomSessionRestoreInPlaceTest, AcceptsAPointWhosePatchReportedAnError)
{
  FakeRoom room;
  room.Reply("POST", "ngnt/open", {{"result", "success"}});
  // What a freshly started room really answers: the point is stored, but rendering the
  // projection fails because nothing has been mapped yet.
  room.Fail("PATCH", "ngnt/map", "No entry and no projection found, nothing sent to websocket");
  room.Reply("GET", "state", json::parse(StateWithPoint("[1]")));

  json snapshot = SnapshotOf({{"ngnt/open", {{"file", "f.root"}}}});
  snapshot["point"] = json::parse("[1]");

  std::string error;
  EXPECT_TRUE(Ndmspc::NRoomSession::RestoreInPlace(snapshot, room.Dispatcher(), error)) << error;
}

TEST(NRoomSessionRestoreInPlaceTest, FailsWhenThePointWasNotKept)
{
  FakeRoom room;
  room.Reply("POST", "ngnt/open", {{"result", "success"}});
  room.Reply("PATCH", "ngnt/map", {{"result", "success"}});
  room.Reply("GET", "state", json::parse(StateWithPoint("[]")));

  json snapshot = SnapshotOf({{"ngnt/open", {{"file", "f.root"}}}});
  snapshot["point"] = json::parse("[1]");

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::RestoreInPlace(snapshot, room.Dispatcher(), error));
  EXPECT_NE(error.find("did not keep the state point"), std::string::npos);
}

TEST(NRoomSessionRestoreInPlaceTest, StopsAtAFailingAction)
{
  FakeRoom room;
  room.Fail("POST", "ngnt/open", "ngnt/open failed: cannot open file");

  json snapshot = SnapshotOf({{"ngnt/open", {{"file", "missing.root"}}}, {"ngnt/reshape", {{"binningName", "b0"}}}});

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::RestoreInPlace(snapshot, room.Dispatcher(), error));
  EXPECT_NE(error.find("cannot open file"), std::string::npos);
  ASSERT_EQ(room.calls.size(), 1u);
}

TEST(NRoomSessionRestoreInPlaceTest, RefusesAnUnknownAction)
{
  FakeRoom room;

  const json snapshot = SnapshotOf({{"room/close", json::object()}});

  std::string error;
  EXPECT_FALSE(Ndmspc::NRoomSession::RestoreInPlace(snapshot, room.Dispatcher(), error));
  EXPECT_NE(error.find("cannot be replayed"), std::string::npos);
  EXPECT_TRUE(room.calls.empty());
}

} // namespace
