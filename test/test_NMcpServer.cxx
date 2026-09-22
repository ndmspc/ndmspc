#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include <THttpCallArg.h>

#include "ndmspc/http/NMcpServer.h"
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NSchemaBuilder.h"

namespace {

// Non-capturing free functions so they convert to Ndmspc::NHttpFuncPtr.
void EchoHandler(std::string method, json & in, json & out, json & /*wsOut*/,
                 std::map<std::string, TObject *> & /*objects*/)
{
  out["result"]        = "success";
  out["echo"]["method"] = method;
  out["echo"]["in"]     = in;
}

void FailingHandler(std::string /*method*/, json & /*in*/, json & out, json & /*wsOut*/,
                    std::map<std::string, TObject *> & /*objects*/)
{
  out["error"] = "boom";
}

Ndmspc::NHttpServer * MakeServer(std::map<std::string, Ndmspc::NHttpFuncPtr> handlers = {})
{
  auto * serv = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  serv->SetHttpHandlers(std::move(handlers));
  Ndmspc::gNdmspcMcpTools = nullptr; // isolate tests from any metadata registry
  return serv;
}

json ToolNames(const json & toolsResult)
{
  json names = json::array();
  for (const auto & tool : toolsResult["tools"]) {
    names.push_back(tool["name"]);
  }
  return names;
}

} // namespace

TEST(NMcpServerTest, InitializeReturnsCapabilitiesAndServerInfo)
{
  auto *              serv = MakeServer();
  Ndmspc::NMcpServer  mcp(serv);

  json request  = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2025-06-18"}}}};
  json response = mcp.Handle(request);

  ASSERT_TRUE(response.is_object());
  EXPECT_EQ(response["jsonrpc"], "2.0");
  EXPECT_EQ(response["id"], 1);
  EXPECT_EQ(response["result"]["protocolVersion"], "2025-06-18");
  EXPECT_FALSE(response["result"]["capabilities"]["tools"]["listChanged"].get<bool>());
  EXPECT_EQ(response["result"]["serverInfo"]["name"], "ndmspc-ngnt");
  EXPECT_FALSE(response["result"]["serverInfo"]["version"].get<std::string>().empty());

  delete serv;
}

TEST(NMcpServerTest, ToolsListMirrorsRegisteredHandlersAndSkipsExcluded)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"]    = EchoHandler;
  handlers["ngnt/reshape"] = EchoHandler;
  handlers["debug"]        = EchoHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", json::object()}});
  const json names = ToolNames(response["result"]);

  EXPECT_NE(std::find(names.begin(), names.end(), "ngnt_open"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "ngnt_reshape"), names.end());
  EXPECT_EQ(std::find(names.begin(), names.end(), "debug"), names.end());

  delete serv;
}

TEST(NMcpServerTest, ToolsListUsesInspectorSchemaAndAddsMethod)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto * serv = MakeServer(std::move(handlers));
  serv->GetWorkspace()["open"] = Ndmspc::NSchemaBuilder().String("file").Default("test.root").Build();

  Ndmspc::NMcpServer mcp(serv);
  json response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", json::object()}});

  const json & tool = response["result"]["tools"][0];
  EXPECT_EQ(tool["name"], "ngnt_open");
  EXPECT_EQ(tool["inputSchema"]["type"], "object");
  EXPECT_EQ(tool["inputSchema"]["properties"]["file"]["type"], "string");
  EXPECT_EQ(tool["inputSchema"]["properties"]["file"]["default"], "test.root");
  EXPECT_EQ(tool["inputSchema"]["properties"]["method"]["default"], "POST");

  delete serv;
}

TEST(NMcpServerTest, ToolsCallRoutesThroughServerAndEchoesArguments)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle({{"jsonrpc", "2.0"},
                              {"id", 3},
                              {"method", "tools/call"},
                              {"params", {{"name", "ngnt_open"}, {"arguments", {{"file", "a.root"}}}}}});

  ASSERT_TRUE(response.contains("result"));
  EXPECT_FALSE(response["result"]["isError"].get<bool>());
  ASSERT_TRUE(response["result"].contains("structuredContent"));
  EXPECT_EQ(response["result"]["structuredContent"]["echo"]["method"], "POST");
  EXPECT_EQ(response["result"]["structuredContent"]["echo"]["in"]["file"], "a.root");

  delete serv;
}

TEST(NMcpServerTest, ToolsCallHonoursMethodArgument)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle({{"jsonrpc", "2.0"},
                              {"id", 4},
                              {"method", "tools/call"},
                              {"params", {{"name", "ngnt_open"}, {"arguments", {{"method", "GET"}}}}}});

  EXPECT_EQ(response["result"]["structuredContent"]["echo"]["method"], "GET");
  EXPECT_FALSE(response["result"]["structuredContent"]["echo"]["in"].contains("method"));

  delete serv;
}

TEST(NMcpServerTest, FailingToolSetsIsError)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = FailingHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle({{"jsonrpc", "2.0"},
                              {"id", 5},
                              {"method", "tools/call"},
                              {"params", {{"name", "ngnt_open"}, {"arguments", json::object()}}}});

  EXPECT_TRUE(response["result"]["isError"].get<bool>());
  EXPECT_EQ(response["result"]["structuredContent"]["error"], "boom");

  delete serv;
}

TEST(NMcpServerTest, UnknownToolReturnsInvalidParams)
{
  auto *             serv = MakeServer();
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle(
      {{"jsonrpc", "2.0"}, {"id", 6}, {"method", "tools/call"}, {"params", {{"name", "nope"}}}});

  ASSERT_TRUE(response.contains("error"));
  EXPECT_EQ(response["error"]["code"], -32602);

  delete serv;
}

TEST(NMcpServerTest, UnknownMethodReturnsMethodNotFound)
{
  auto *             serv = MakeServer();
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 7}, {"method", "does/not/exist"}});

  ASSERT_TRUE(response.contains("error"));
  EXPECT_EQ(response["error"]["code"], -32601);

  delete serv;
}

TEST(NMcpServerTest, HandleTextReportsParseError)
{
  auto *             serv = MakeServer();
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.HandleText("not json");
  ASSERT_TRUE(response.contains("error"));
  EXPECT_EQ(response["error"]["code"], -32700);
  EXPECT_TRUE(response["id"].is_null());

  delete serv;
}

TEST(NMcpServerTest, NotificationsProduceNoResponse)
{
  auto *             serv = MakeServer();
  Ndmspc::NMcpServer mcp(serv);

  json request = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
  EXPECT_TRUE(mcp.Handle(request).is_null());

  delete serv;
}

TEST(NMcpServerTest, MetadataFromRegistryDrivesToolFields)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto * serv = MakeServer(std::move(handlers));

  Ndmspc::NMcpToolMap tools;
  tools["ngnt/open"] = {
      .description = "Custom description from the macro.",
      .title       = "Open tree",
      .methods     = {"GET", "DELETE"},
      .inputSchema = {{"properties", {{"file", {{"type", "string"}, {"default", "x.root"}}}}}},
  };
  Ndmspc::gNdmspcMcpTools = &tools;

  Ndmspc::NMcpServer mcp(serv);
  json response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 10}, {"method", "tools/list"}, {"params", json::object()}});

  const json & tool = response["result"]["tools"][0];
  EXPECT_EQ(tool["name"], "ngnt_open");
  EXPECT_EQ(tool["description"], "Custom description from the macro.");
  EXPECT_EQ(tool["title"], "Open tree");
  EXPECT_EQ(tool["inputSchema"]["properties"]["method"]["enum"], json::array({"GET", "DELETE"}));
  EXPECT_EQ(tool["inputSchema"]["properties"]["method"]["default"], "GET");
  EXPECT_EQ(tool["inputSchema"]["properties"]["file"]["default"], "x.root");

  Ndmspc::gNdmspcMcpTools = nullptr;
  delete serv;
}

TEST(NMcpServerTest, HiddenActionIsNotListedNorCallable)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto * serv = MakeServer(std::move(handlers));

  Ndmspc::NMcpToolMap tools;
  tools["ngnt/open"]      = Ndmspc::NMcpToolInfo{.hidden = true};
  Ndmspc::gNdmspcMcpTools = &tools;

  Ndmspc::NMcpServer mcp(serv);
  json listed = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 11}, {"method", "tools/list"}, {"params", json::object()}});
  EXPECT_TRUE(listed["result"]["tools"].empty());

  json called =
      mcp.Handle({{"jsonrpc", "2.0"}, {"id", 12}, {"method", "tools/call"}, {"params", {{"name", "ngnt_open"}}}});
  EXPECT_EQ(called["error"]["code"], -32602);

  Ndmspc::gNdmspcMcpTools = nullptr;
  delete serv;
}

TEST(NMcpServerTest, GenericDescriptionFallbackWithoutMetadata)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  json response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 13}, {"method", "tools/list"}, {"params", json::object()}});
  EXPECT_EQ(response["result"]["tools"][0]["description"], "NGnTree action 'ngnt/open'.");

  delete serv;
}

TEST(NMcpServerTest, McpEndpointDisabledByDefault)
{
  auto * serv = MakeServer();
  EXPECT_FALSE(serv->IsMcpEnabled());
  delete serv;
}

TEST(NMcpServerTest, DisabledMcpEndpointRejectsRequests)
{
  auto * serv = MakeServer();
  ASSERT_FALSE(serv->IsMcpEnabled());

  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod("POST");
  arg->SetPathName("api");
  arg->SetFileName("mcp");
  arg->SetPostData(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");
  serv->ProcessRequest(arg);

  std::string content(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
  EXPECT_NE(content.find("MCP endpoint is disabled"), std::string::npos);

  delete serv;
}

TEST(NMcpServerTest, EnabledMcpEndpointAnswersRequests)
{
  auto * serv = MakeServer();
  serv->SetMcpEnabled(true);
  ASSERT_TRUE(serv->IsMcpEnabled());

  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod("POST");
  arg->SetPathName("api");
  arg->SetFileName("mcp");
  arg->SetPostData(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":{}})");
  serv->ProcessRequest(arg);

  std::string content(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
  EXPECT_NE(content.find("\"jsonrpc\""), std::string::npos);
  EXPECT_EQ(content.find("MCP endpoint is disabled"), std::string::npos);

  delete serv;
}

// The inspector schema is empty until an action has run, so tools must accept
// undeclared parameters; otherwise clients prune them (e.g. 'file' for ngnt/open).
TEST(NMcpServerTest, InputSchemaAllowsUndeclaredArgumentsWhenNoInspectorSchema)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  json          response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 20}, {"method", "tools/list"}, {"params", json::object()}});
  const json &  schema   = response["result"]["tools"][0]["inputSchema"];
  EXPECT_TRUE(schema["additionalProperties"].get<bool>());

  delete serv;
}

TEST(NMcpServerTest, MacroInputSchemaPropertiesAreMerged)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto * serv = MakeServer(std::move(handlers));

  Ndmspc::NMcpToolMap tools;
  tools["ngnt/open"] = {
      .description = "d",
      .inputSchema = {{"properties", {{"file", {{"type", "string"}}}}}},
  };
  Ndmspc::gNdmspcMcpTools = &tools;

  Ndmspc::NMcpServer mcp(serv);
  json response = mcp.Handle({{"jsonrpc", "2.0"}, {"id", 21}, {"method", "tools/list"}, {"params", json::object()}});

  EXPECT_EQ(response["result"]["tools"][0]["inputSchema"]["properties"]["file"]["type"], "string");

  Ndmspc::gNdmspcMcpTools = nullptr;
  delete serv;
}

TEST(NMcpServerTest, AToolCallRunsAsTheCallerThatReachedTheEndpoint)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ngnt/open"] = EchoHandler;

  auto *             serv = MakeServer(std::move(handlers));
  Ndmspc::NMcpServer mcp(serv);

  // A tool call is dispatched as a request of its own, built from the tool's arguments alone, so it
  // carries none of the headers that would say who asked: the transport states the caller instead.
  Ndmspc::NRequestIdentity identity;
  identity.subject  = "subject-1";
  identity.username = "alice";
  identity.email    = "alice@example.com";
  identity.verified = true;
  mcp.SetCallerIdentity(identity);

  json request = {{"jsonrpc", "2.0"},
                  {"id", 22},
                  {"method", "tools/call"},
                  {"params", {{"name", "ngnt_open"}, {"arguments", {{"file", "x.root"}}}}}};
  const json response = mcp.Handle(request);
  const json & in     = response["result"]["structuredContent"]["echo"]["in"];
  EXPECT_EQ(in["file"], "x.root");
  EXPECT_EQ(in["_identity"]["user"], "alice");
  EXPECT_EQ(in["_identity"]["email"], "alice@example.com");
  EXPECT_EQ(in["_identity"]["subject"], "subject-1");
  EXPECT_TRUE(in["_identity"]["verified"].get<bool>());

  // A caller cannot name itself: what the endpoint established replaces anything it sent.
  json spoofed = {{"jsonrpc", "2.0"},
                  {"id", 23},
                  {"method", "tools/call"},
                  {"params", {{"name", "ngnt_open"},
                              {"arguments", {{"_identity", {{"user", "root"}, {"verified", true}}}}}}}};
  const json spoofedResponse = mcp.Handle(spoofed);
  EXPECT_EQ(spoofedResponse["result"]["structuredContent"]["echo"]["in"]["_identity"]["user"], "alice");

  delete serv;
}
