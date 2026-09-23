// Unit tests for NRoomClient: the JSON-RPC request it builds and how it reads the
// router's reply. The HTTP transport is injected, so no network or cluster is needed
// (the same approach test_NOidcTokenClient.cxx takes with a fake IOidcTokenHttpClient).
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "ndmspc/http/NRoomClient.h"

namespace {

/// @brief Records the request it was given and replays a canned response.
class FakeRoomHttpClient : public Ndmspc::IRoomHttpClient {
  public:
  std::string                        url;
  std::string                        body;
  std::map<std::string, std::string> headers;
  std::string                        responseBody;
  int                                responseStatus{200};
  bool                               throwOnPost{false};
  std::string                        throwMessage{"Connection refused"};
  int                                postCount{0};

  Ndmspc::NHttpResponse Post(const std::string & requestUrl, const std::string & requestBody,
                             const std::map<std::string, std::string> & requestHeaders) override
  {
    url    = requestUrl;
    body   = requestBody;
    headers = requestHeaders;
    ++postCount;
    if (throwOnPost) throw std::runtime_error(throwMessage);

    Ndmspc::NHttpResponse response;
    response.status = responseStatus;
    response.body   = responseBody;
    return response;
  }

  /// @brief The JSON-RPC message the client sent.
  json Request() const { return json::parse(body); }
};

/// @brief Wrap a handler envelope the way the router's MCP endpoint does.
/// @param handler The handler's own JSON (e.g. {"result":"success","payload":{...}}).
/// @param isError The MCP result's isError flag.
/// @param withStructuredContent When false, only content[0].text carries the handler JSON.
std::string McpEnvelope(const json & handler, bool isError = false, bool withStructuredContent = true)
{
  json content = json::array();
  content.push_back({{"type", "text"}, {"text", handler.dump()}});

  json result;
  result["content"] = content;
  if (withStructuredContent) result["structuredContent"] = handler;
  result["isError"] = isError;

  json envelope;
  envelope["jsonrpc"] = "2.0";
  envelope["id"]      = 1;
  envelope["result"]  = result;
  return envelope.dump();
}

/// @brief A router reply carrying only a JSON-RPC error (no tool result).
std::string RpcError(const std::string & message, int code = -32602)
{
  json envelope;
  envelope["jsonrpc"]         = "2.0";
  envelope["id"]              = 1;
  envelope["error"]["code"]   = code;
  envelope["error"]["message"] = message;
  return envelope.dump();
}

class NRoomClientTest : public ::testing::Test {
  protected:
  std::shared_ptr<FakeRoomHttpClient> fake{std::make_shared<FakeRoomHttpClient>()};

  Ndmspc::NRoomClient Client(const std::string & bearerToken = "")
  {
    return Ndmspc::NRoomClient("http://router.test/api/mcp", bearerToken, fake);
  }
};

// ---------------------------------------------------------------------------
//  Endpoint derivation
// ---------------------------------------------------------------------------

TEST(NRoomClientEndpointTest, DerivesMcpEndpointFromAServerUrl)
{
  EXPECT_EQ(Ndmspc::NRoomClient::McpEndpoint("http://localhost:8080"), "http://localhost:8080/api/mcp");
}

TEST(NRoomClientEndpointTest, ToleratesTrailingSlashes)
{
  EXPECT_EQ(Ndmspc::NRoomClient::McpEndpoint("http://localhost:8080/"), "http://localhost:8080/api/mcp");
  EXPECT_EQ(Ndmspc::NRoomClient::McpEndpoint("http://localhost:8080///"), "http://localhost:8080/api/mcp");
}

TEST(NRoomClientEndpointTest, KeepsAnEndpointThatIsAlreadyMcp)
{
  EXPECT_EQ(Ndmspc::NRoomClient::McpEndpoint("http://localhost:8080/api/mcp"), "http://localhost:8080/api/mcp");
}

// ---------------------------------------------------------------------------
//  Request shape
// ---------------------------------------------------------------------------

TEST_F(NRoomClientTest, ListSendsAToolsCallForRoomListWithGet)
{
  fake->responseBody = McpEnvelope({{"result", "success"}, {"payload", {{"rooms", json::array()}, {"ttl", 3600}}}});

  const Ndmspc::NRoomListResult list = Client().List();

  ASSERT_TRUE(list.ok) << list.error;
  EXPECT_EQ(fake->url, "http://router.test/api/mcp");
  EXPECT_EQ(fake->headers.at("Content-Type"), "application/json");

  const json request = fake->Request();
  EXPECT_EQ(request["jsonrpc"], "2.0");
  EXPECT_EQ(request["method"], "tools/call");
  EXPECT_EQ(request["params"]["name"], "room_list");
  EXPECT_EQ(request["params"]["arguments"]["method"], "GET");
  // room_list takes no room argument.
  EXPECT_FALSE(request["params"]["arguments"].contains("room"));
  EXPECT_TRUE(request.contains("id"));
}

TEST_F(NRoomClientTest, OpenSendsPostWithTheRoomInTheBody)
{
  fake->responseBody = McpEnvelope({{"result", "success"},
                                    {"payload",
                                     {{"room", "abcd123"},
                                      {"name", "ndmspc-room-abcd123"},
                                      {"revision", "ndmspc-room-abcd123-00001"},
                                      {"param", "room"},
                                      {"url", "?room=abcd123"},
                                      {"ttl", 3600}}}});

  const Ndmspc::NRoomResult result = Client().Open("abcd123");

  ASSERT_TRUE(result.ok) << result.error;
  const json request = fake->Request();
  EXPECT_EQ(request["params"]["name"], "room_open");
  EXPECT_EQ(request["params"]["arguments"]["method"], "POST");
  EXPECT_EQ(request["params"]["arguments"]["room"], "abcd123");
  EXPECT_EQ(result.payload["url"], "?room=abcd123");
  EXPECT_EQ(result.payload["ttl"], 3600);
}

TEST_F(NRoomClientTest, StatusUsesGetAndCloseUsesDelete)
{
  fake->responseBody = McpEnvelope({{"result", "success"}, {"payload", {{"room", "r1"}}}});
  ASSERT_TRUE(Client().Status("r1").ok);
  EXPECT_EQ(fake->Request()["params"]["name"], "room_status");
  EXPECT_EQ(fake->Request()["params"]["arguments"]["method"], "GET");
  EXPECT_EQ(fake->Request()["params"]["arguments"]["room"], "r1");

  ASSERT_TRUE(Client().Close("r1").ok);
  EXPECT_EQ(fake->Request()["params"]["name"], "room_close");
  EXPECT_EQ(fake->Request()["params"]["arguments"]["method"], "DELETE");
}

TEST_F(NRoomClientTest, EachCallGetsADistinctRequestId)
{
  fake->responseBody = McpEnvelope({{"result", "success"}, {"payload", json::object()}});
  auto client        = Client();
  client.List();
  const long long first = fake->Request()["id"].get<long long>();
  client.List();
  const long long second = fake->Request()["id"].get<long long>();
  EXPECT_NE(first, second);
}

// ---------------------------------------------------------------------------
//  Responses
// ---------------------------------------------------------------------------

TEST_F(NRoomClientTest, ListParsesRoomsAndTtl)
{
  const json payload = {
      {"rooms",
       json::array({{{"name", "ndmspc-room-idle"},
                     {"room", "idle"},
                     {"revision", "ndmspc-room-idle-00001"},
                     {"lastSeen", 1730000000},
                     {"ready", true},
                     {"replicas", 0},
                     {"active", false}},
                    {{"name", "ndmspc-room-busy"},
                     {"room", "busy"},
                     {"revision", "ndmspc-room-busy-00002"},
                     {"lastSeen", 1730000123},
                     {"ready", true},
                     {"replicas", 2},
                     {"active", true}}})},
      {"ttl", 3600}};
  fake->responseBody = McpEnvelope({{"result", "success"}, {"payload", payload}});

  const Ndmspc::NRoomListResult list = Client().List();

  ASSERT_TRUE(list.ok) << list.error;
  ASSERT_EQ(list.rooms.size(), 2u);
  EXPECT_EQ(list.ttl, 3600);

  EXPECT_EQ(list.rooms[0].room, "idle");
  EXPECT_EQ(list.rooms[0].name, "ndmspc-room-idle");
  EXPECT_EQ(list.rooms[0].revision, "ndmspc-room-idle-00001");
  EXPECT_EQ(list.rooms[0].lastSeen, 1730000000);
  EXPECT_TRUE(list.rooms[0].ready);
  EXPECT_EQ(list.rooms[0].replicas, 0);
  EXPECT_FALSE(list.rooms[0].active);

  EXPECT_EQ(list.rooms[1].room, "busy");
  EXPECT_EQ(list.rooms[1].replicas, 2);
  EXPECT_TRUE(list.rooms[1].active);
}

TEST_F(NRoomClientTest, IdleRoomWithoutAnActiveFlagIsDerivedFromReplicas)
{
  // An idle room keeps its Service but runs no pods; a router that omits "active"
  // must not make the room look busy.
  const json rooms = json::array({{{"room", "idle"}, {"replicas", 0}}, {{"room", "busy"}, {"replicas", 3}}});
  fake->responseBody = McpEnvelope({{"result", "success"}, {"payload", {{"rooms", rooms}, {"ttl", 60}}}});

  const Ndmspc::NRoomListResult list = Client().List();

  ASSERT_TRUE(list.ok) << list.error;
  ASSERT_EQ(list.rooms.size(), 2u);
  EXPECT_FALSE(list.rooms[0].active);
  EXPECT_TRUE(list.rooms[1].active);
}

TEST_F(NRoomClientTest, FallsBackToTheTextContentWhenStructuredContentIsAbsent)
{
  fake->responseBody =
      McpEnvelope({{"result", "success"}, {"payload", {{"room", "r1"}, {"name", "ndmspc-room-r1"}}}}, false, false);

  const Ndmspc::NRoomResult result = Client().Status("r1");

  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.payload["name"], "ndmspc-room-r1");
}

TEST_F(NRoomClientTest, HandlerFailureIsReportedWithTheRoutersMessage)
{
  fake->responseBody = McpEnvelope({{"result", "failure"}, {"error", "Missing room id (send it in the body as {\"room\": \"<id>\"})"}},
                                   true);

  const Ndmspc::NRoomResult result = Client().Status("");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error, "Missing room id (send it in the body as {\"room\": \"<id>\"})");
}

// ---------------------------------------------------------------------------
//  Failure paths - none of these may throw
// ---------------------------------------------------------------------------

TEST_F(NRoomClientTest, RpcErrorIsReportedWithItsMessage)
{
  fake->responseBody = RpcError("Unknown tool: room_nope");

  const Ndmspc::NRoomResult result = Client().Open("r1");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error, "Unknown tool: room_nope");
}

TEST_F(NRoomClientTest, PlainTextErrorContentIsUsedAsTheMessage)
{
  json content = json::array();
  content.push_back({{"type", "text"}, {"text", "Unknown tool: room_nope"}});
  json envelope;
  envelope["jsonrpc"]           = "2.0";
  envelope["id"]                = 1;
  envelope["result"]["content"] = content;
  envelope["result"]["isError"] = true;
  fake->responseBody            = envelope.dump();

  const Ndmspc::NRoomResult result = Client().Open("r1");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error, "Unknown tool: room_nope");
}

TEST_F(NRoomClientTest, TransportFailureNamesTheEndpointAndIsNotRaised)
{
  fake->throwOnPost  = true;
  fake->throwMessage = "Connection refused";

  const Ndmspc::NRoomResult result = Client().Open("r1");

  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("http://router.test/api/mcp"), std::string::npos);
  EXPECT_NE(result.error.find("Connection refused"), std::string::npos);
}

TEST_F(NRoomClientTest, UnauthorisedResponseHintsAtAuthentication)
{
  fake->responseStatus = 401;

  const Ndmspc::NRoomResult result = Client().Open("r1");

  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("HTTP 401"), std::string::npos);
  EXPECT_NE(result.error.find("authentication"), std::string::npos);
}

TEST_F(NRoomClientTest, NonJsonResponseIsReported)
{
  fake->responseBody = "<html>not the router</html>";

  const Ndmspc::NRoomListResult result = Client().List();

  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("not JSON"), std::string::npos);
}

TEST_F(NRoomClientTest, DisabledMcpEndpointIsReportedWithAHint)
{
  fake->responseBody = RpcError("MCP endpoint is disabled", -32601);

  std::string error;
  EXPECT_FALSE(Client().Initialize(error));
  EXPECT_NE(error.find("MCP endpoint is disabled"), std::string::npos);
  EXPECT_NE(error.find("--mcp true"), std::string::npos);
}

// ---------------------------------------------------------------------------
//  Handshake and authentication
// ---------------------------------------------------------------------------

TEST_F(NRoomClientTest, InitializePerformsTheMcpHandshake)
{
  fake->responseBody =
      R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"ndmspc-ngnt"}}})";

  std::string error;
  EXPECT_TRUE(Client().Initialize(error)) << error;
  EXPECT_TRUE(error.empty());

  const json request = fake->Request();
  EXPECT_EQ(request["method"], "initialize");
  EXPECT_EQ(request["params"]["protocolVersion"], "2025-06-18");
  EXPECT_EQ(request["params"]["clientInfo"]["name"], "ndmspc-room-tui");
}

TEST_F(NRoomClientTest, BearerTokenIsSentOnlyWhenSupplied)
{
  fake->responseBody = McpEnvelope({{"result", "success"}, {"payload", {{"rooms", json::array()}, {"ttl", 1}}}});

  Client().List();
  EXPECT_EQ(fake->headers.count("Authorization"), 0u);

  Client("a-token").List();
  EXPECT_EQ(fake->headers.at("Authorization"), "Bearer a-token");
}

TEST_F(NRoomClientTest, BackupSendsAToolsCallAndHandsBackTheDocument)
{
  const json document = {{"version", 1}, {"router", {{"param", "room"}}}, {"rooms", json::array()}};
  fake->responseBody  = McpEnvelope({{"result", "success"}, {"payload", document}});

  const Ndmspc::NRoomResult result = Client().Backup();

  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.payload, document);

  const json request = fake->Request();
  EXPECT_EQ(request["params"]["name"], "room_backup");
  EXPECT_EQ(request["params"]["arguments"]["method"], "GET");
  EXPECT_FALSE(request["params"]["arguments"].contains("room"));
}

// The table and detail pane read these, so the parse is what the TUI shows: the size the room runs
// at, both sides of what that allows, and why it died last (which a room the kernel killed never
// reports itself).
TEST_F(NRoomClientTest, ListReadsTheProfileResourcesAndLastError)
{
  const json room = json::parse(R"({
    "room": "test", "state": "ready", "profile": "small", "pods": "1/1",
    "resources": {"requests": {"cpu": "250m", "memory": "256Mi"},
                  "limits": {"cpu": "1", "memory": "1Gi"}},
    "lastError": {"reason": "OOMKilled", "exitCode": 137,
                  "message": "container was OOM killed", "at": "2026-09-23T18:00:00Z"}
  })");
  fake->responseBody =
      McpEnvelope({{"result", "success"}, {"payload", {{"rooms", json::array({room})}, {"ttl", 3600}}}});

  const Ndmspc::NRoomListResult list = Client().List();

  ASSERT_TRUE(list.ok) << list.error;
  ASSERT_EQ(list.rooms.size(), 1u);
  const Ndmspc::NRoomInfo & info = list.rooms[0];
  EXPECT_EQ(info.profile, "small");
  EXPECT_EQ(info.cpuRequest, "250m");
  EXPECT_EQ(info.memoryRequest, "256Mi");
  EXPECT_EQ(info.cpuLimit, "1");
  EXPECT_EQ(info.memoryLimit, "1Gi");
  EXPECT_EQ(info.lastErrorReason, "OOMKilled");
  EXPECT_EQ(info.lastErrorExit, 137);
  EXPECT_EQ(info.lastErrorMessage, "container was OOM killed");
}

TEST_F(NRoomClientTest, ListLeavesWhatIsAbsentEmptyRatherThanInventingIt)
{
  fake->responseBody = McpEnvelope({{"result", "success"},
                                    {"payload", {{"rooms", json::array({{{"room", "plain"}, {"state", "ready"}}})}, {"ttl", 0}}}});

  const Ndmspc::NRoomListResult list = Client().List();

  ASSERT_TRUE(list.ok) << list.error;
  ASSERT_EQ(list.rooms.size(), 1u);
  const Ndmspc::NRoomInfo & info = list.rooms[0];
  // A room from before profiles and limits existed carries neither, and says nothing about a death it
  // never had. Each is a straight read of the payload, so the empty string is what "absent" looks like.
  EXPECT_TRUE(info.profile.empty());
  EXPECT_TRUE(info.cpuRequest.empty());
  EXPECT_TRUE(info.memoryRequest.empty());
  EXPECT_TRUE(info.cpuLimit.empty());
  EXPECT_TRUE(info.memoryLimit.empty());
  EXPECT_TRUE(info.lastErrorReason.empty());
  EXPECT_TRUE(info.lastErrorMessage.empty());
  // -1, because that is what `NUtils::GetJsonInt` reads an absent member as, and what the struct
  // defaults to. Deliberately not 0: a container that exits cleanly reports 0, so 0 would be
  // indistinguishable from "this room never died".
  EXPECT_EQ(info.lastErrorExit, -1);
  EXPECT_EQ(info.lastErrorAt, -1);
}

TEST_F(NRoomClientTest, RestoreCarriesTheDocumentAndSendsPost)
{
  const json document = {{"version", 1}, {"rooms", json::array({{{"room", "test"}}})}};
  fake->responseBody  = McpEnvelope({{"result", "success"},
                                     {"payload",
                                      {{"restored", json::array({{{"room", "test"}, {"revision", "ndmspc-room-test-00001"}}})},
                                       {"failed", json::array()}}}});

  const Ndmspc::NRoomResult result = Client().Restore(document);

  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.payload["restored"].size(), 1u);
  EXPECT_TRUE(result.payload["failed"].empty());

  const json request = fake->Request();
  EXPECT_EQ(request["params"]["name"], "room_restore");
  EXPECT_EQ(request["params"]["arguments"]["method"], "POST");
  EXPECT_EQ(request["params"]["arguments"]["document"], document);
}

TEST_F(NRoomClientTest, RestoreSurfacesAPartialFailureInThePayload)
{
  const json document = {{"version", 1}, {"rooms", json::array()}};
  fake->responseBody  = McpEnvelope(
      {{"result", "success"},
       {"payload",
        {{"restored", json::array()},
         {"failed", json::array({{{"room", "gone"}, {"error", "ngnt/open failed: cannot open file"}}})}}}});

  const Ndmspc::NRoomResult result = Client().Restore(document);

  // The restore itself succeeded; which rooms came back is the caller's business - the CLI
  // turns a non-empty failure list into a non-zero exit.
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(result.payload["failed"].size(), 1u);
  EXPECT_EQ(result.payload["failed"][0]["error"], "ngnt/open failed: cannot open file");
}

} // namespace
