#include <gtest/gtest.h>

#include "ndmspc/http/NBaseActions.h"
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NInstanceTree.h"

#include <THttpCallArg.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <string>

namespace {

using Ndmspc::NHttpServer;

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

} // namespace

TEST(NHttpServerRuntimeEnvTest, OnlyViteNamesAreHandedToThePage)
{
  EnvGuard setting("VITE_NDMSPC_SERVICES", "rooms,room,tools");
  // Everything else the process was started with is internal: it must not reach the page.
  EnvGuard internal("NDMSPC_TEST_INTERNAL", "do-not-leak");

  const json env = NHttpServer::RuntimeEnv();
  EXPECT_EQ(env["VITE_NDMSPC_SERVICES"].get<std::string>(), "rooms,room,tools");
  EXPECT_EQ(env.find("NDMSPC_TEST_INTERNAL"), env.end());
}

TEST(NHttpServerRuntimeEnvTest, AValueCannotCloseTheScriptElement)
{
  const std::string value = "</script><script>alert(1)</script>";
  const std::string text  = NHttpServer::JsonForHtml(json{{"VITE_X", value}});

  EXPECT_EQ(text.find("</script>"), std::string::npos);
  EXPECT_EQ(text.find("<script>"), std::string::npos);
  // ... and it is still the same JSON value.
  EXPECT_EQ(json::parse(text)["VITE_X"].get<std::string>(), value);
}

TEST(NHttpServerRuntimeEnvTest, TheEscapesCoverTheOtherMarkupAndLineTerminators)
{
  const std::string value = "a\u2028b\u2029c&<>";
  const std::string text  = NHttpServer::JsonForHtml(json{{"VITE_X", value}});

  EXPECT_EQ(text.find("\xE2\x80\xA8"), std::string::npos);
  EXPECT_EQ(text.find("&"), std::string::npos);
  EXPECT_EQ(text.find("<"), std::string::npos);
  EXPECT_EQ(json::parse(text)["VITE_X"].get<std::string>(), value);
}

TEST(NHttpServerRuntimeEnvTest, TheSettingsGoInAheadOfThePagesOwnScripts)
{
  const std::string html =
      "<!doctype html>\n<html><head>\n<script type=\"module\" src=\"/assets/app.js\"></script>\n</head><body></body></html>";
  const std::string page = NHttpServer::InjectRuntimeEnv(html, json{{"VITE_NDMSPC_SERVICES", "room"}});

  const auto injected = page.find("window.__NDMSPC_ENV__");
  ASSERT_NE(injected, std::string::npos);
  EXPECT_LT(injected, page.find("/assets/app.js")); // ahead of the app's own script
  EXPECT_NE(page.find("<!doctype html>"), std::string::npos);

  // Nothing to inject leaves the page alone, so the caller serves the built one.
  EXPECT_EQ(NHttpServer::InjectRuntimeEnv(html, json::object()), "");
}

TEST(NHttpServerRuntimeEnvTest, APageWithoutAHeadStillGetsTheSettingsFirst)
{
  const std::string page = NHttpServer::InjectRuntimeEnv("<p>hi</p>", json{{"VITE_X", "1"}});
  EXPECT_EQ(page.rfind("<script>window.__NDMSPC_ENV__", 0), 0u);
}

TEST(NBaseActionsTest, RegistersTheServersOwnActionsAndNotTheDebugHelper)
{
  // The map a CLI wires up, so the actions registered here are the ones it serves.
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  Ndmspc::NMcpToolMap                         tools;
  Ndmspc::NHttpHandlerMap *                   previousHandlers = Ndmspc::gNdmspcHttpHandlers;
  Ndmspc::NMcpToolMap *                       previousTools    = Ndmspc::gNdmspcMcpTools;
  Ndmspc::gNdmspcHttpHandlers = &handlers;
  Ndmspc::gNdmspcMcpTools      = &tools;

  EXPECT_TRUE(Ndmspc::RegisterBaseActions());

  // What the server describes itself with, as handlers and as MCP tools.
  EXPECT_NE(handlers.find("ndmspc/health"), handlers.end());
  EXPECT_NE(handlers.find("ndmspc/state"), handlers.end());
  // The room's tool group: GET reports it, PATCH sets it for every client.
  EXPECT_NE(handlers.find("ndmspc/group"), handlers.end());
  // The room's sessions (one per open file): GET lists them, PATCH makes one active.
  EXPECT_NE(handlers.find("ndmspc/session"), handlers.end());
  // The heartbeat cadence: GET reports it, POST sets it, so a view can ask for finer readings while
  // something long is running and put the deployment's own back afterwards.
  EXPECT_NE(handlers.find("ndmspc/heartbeat"), handlers.end());
  EXPECT_NE(tools.find("ndmspc/health"), tools.end());
  EXPECT_NE(tools.find("ndmspc/state"), tools.end());
  EXPECT_NE(tools.find("ndmspc/group"), tools.end());
  EXPECT_NE(tools.find("ndmspc/session"), tools.end());
  EXPECT_NE(tools.find("ndmspc/heartbeat"), tools.end());

  // The debug echo helper is gone with the macro it used to live in: a macro that wants one
  // registers its own.
  EXPECT_EQ(handlers.find("debug"), handlers.end());
  EXPECT_EQ(tools.find("debug"), tools.end());

  // Registering twice is what a deployment that also loads the deprecated toolBase.C shim does.
  EXPECT_TRUE(Ndmspc::RegisterBaseActions());
  EXPECT_EQ(handlers.size(), 5u);

  // A process that never wired a handler map is told so rather than crashing.
  Ndmspc::gNdmspcHttpHandlers = nullptr;
  EXPECT_FALSE(Ndmspc::RegisterBaseActions());

  Ndmspc::gNdmspcHttpHandlers = previousHandlers;
  Ndmspc::gNdmspcMcpTools      = previousTools;
}

namespace {

// Non-capturing, so it converts to Ndmspc::NHttpFuncPtr. Reports success so the action lands in
// the workspace history, which is what says whether a prerequisite has been met.
void SuccessHandler(std::string /*method*/, json & /*in*/, json & out, json & /*wsOut*/,
                    std::map<std::string, TObject *> & /*objects*/)
{
  out["result"] = "success";
}

// One request through the same dispatch /api/* uses, returning the response body.
std::string Request(Ndmspc::NHttpServer * server, const char * method, const char * action)
{
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod(method);
  arg->SetPathName("api");
  arg->SetFileName(action);
  arg->SetPostData("{}");
  server->ProcessRequest(arg);
  return std::string(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
}

// The same, with a body.
std::string RequestJson(Ndmspc::NHttpServer * server, const char * method, const char * action, const json & body)
{
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod(method);
  arg->SetPathName("api");
  arg->SetFileName(action);
  arg->SetPostData(body.dump());
  server->ProcessRequest(arg);
  return std::string(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
}

} // namespace

TEST(NHttpServerToolDependencyTest, ADependentActionIsRefusedUntilItsPrerequisiteRuns)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"]    = SuccessHandler;
  handlers["ndmspc/ngnt/reshape"] = SuccessHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]             = {}; // a prerequisite must itself be a known tool to be enforced
  tools["ndmspc/ngnt/reshape"]          = {.dependsOn = {"ndmspc/ngnt/open"}};
  Ndmspc::gNdmspcMcpTools        = &tools;

  // reshape before open: refused, naming the action to run first.
  const std::string refused = Request(server, "POST", "ndmspc/ngnt/reshape");
  EXPECT_NE(refused.find("\"code\":\"prerequisite_required\""), std::string::npos);
  EXPECT_NE(refused.find("\"required\":\"ndmspc/ngnt/open\""), std::string::npos);

  // open runs, and reshape is admitted once its prerequisite is met.
  EXPECT_NE(Request(server, "POST", "ndmspc/ngnt/open").find("\"result\":\"success\""), std::string::npos);
  EXPECT_EQ(Request(server, "POST", "ndmspc/ngnt/reshape").find("prerequisite_required"), std::string::npos);

  Ndmspc::gNdmspcMcpTools = previous;
  delete server;
}

TEST(NHttpServerToolDependencyTest, ClosingThePrerequisiteRevokesItsDependents)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"]    = SuccessHandler;
  handlers["ndmspc/ngnt/reshape"] = SuccessHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]             = {};
  tools["ndmspc/ngnt/reshape"]          = {.dependsOn = {"ndmspc/ngnt/open"}};
  Ndmspc::gNdmspcMcpTools        = &tools;

  EXPECT_NE(Request(server, "POST", "ndmspc/ngnt/open").find("success"), std::string::npos);
  EXPECT_EQ(Request(server, "POST", "ndmspc/ngnt/reshape").find("prerequisite_required"), std::string::npos);

  // Closing the file (DELETE open) drops the history entries that followed it, so the tools that
  // depended on it are refused again. This is why no separate "satisfied" state is needed.
  Request(server, "DELETE", "ndmspc/ngnt/open");
  EXPECT_NE(Request(server, "POST", "ndmspc/ngnt/reshape").find("prerequisite_required"), std::string::npos);

  Ndmspc::gNdmspcMcpTools = previous;
  delete server;
}

TEST(NHttpServerToolDependencyTest, AnActionWithNoDeclaredPrerequisiteIsNeverGated)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/reshape"] = SuccessHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  Ndmspc::gNdmspcMcpTools        = nullptr; // no tool metadata at all

  EXPECT_NE(Request(server, "POST", "ndmspc/ngnt/reshape").find("\"result\":\"success\""), std::string::npos);

  Ndmspc::gNdmspcMcpTools = previous;
  delete server;
}

// A group whose tools declare dependencies is a combination tree: POST creates a node under the
// active (or named) parent, so several opens and several reshapes per open can coexist.
TEST(NHttpServerCombinationTest, PostCreatesNodesAndBranchesUnderAParent)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"]    = SuccessHandler;
  handlers["ndmspc/ngnt/reshape"] = SuccessHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]             = {};
  tools["ndmspc/ngnt/reshape"]          = {.dependsOn = {"ndmspc/ngnt/open"}};
  Ndmspc::gNdmspcMcpTools        = &tools;

  // An `open` is a root node (its action declares no parent).
  const json open1 = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "a.root"}}));
  ASSERT_TRUE(open1.contains("combination")) << open1.dump();
  const std::string i1 = open1["combination"]["id"];

  // A reshape attaches to the open that is active.
  const json reshape1 = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"binningName", "x"}}));
  ASSERT_TRUE(reshape1.contains("combination")) << reshape1.dump();
  const std::string i2 = reshape1["combination"]["id"];
  EXPECT_EQ(reshape1["combination"]["path"], json::array({i1, i2}));

  // A second reshape is a second node under the same open, not a replacement.
  const json        reshape2 = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"binningName", "y"}}));
  const std::string i3       = reshape2["combination"]["id"];
  ASSERT_NE(i2, i3);

  // Two opens coexist as two roots, each keeping its own children.
  const json        open2 = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "b.root"}}));
  const std::string i4    = open2["combination"]["id"];

  const json tree = server->GetCombinations();
  EXPECT_EQ(tree["nodes"][i1]["children"], json::array({i2, i3}));
  EXPECT_EQ(tree["nodes"][i4]["params"]["file"].get<std::string>(), "b.root");
  EXPECT_EQ(tree["active"], json::array({i4}));

  // A reshape under a reshape is refused: it has to sit under an open.
  const json bad =
      json::parse(RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"path", json::array({i2})}, {"binningName", "z"}}));
  EXPECT_EQ(bad["code"].get<std::string>(), "invalid_combination");

  Ndmspc::gNdmspcMcpTools = previous;
  delete server;
}

TEST(NHttpServerRoomStateTest, TheSnapshotCarriesTheRoomsNamesAndPadsAndGivesThemBack)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  // An open that publishes the file the snapshot's "nothing open" guard reads, and a step that draws, so
  // the room has both the things this is about: a named session and something drawn in it.
  handlers["ndmspc/browser/open"] = [](std::string, json & in, json & out, json & wsOut,
                                std::map<std::string, TObject *> &) {
    wsOut["workspace"]["open"]["properties"]["file"]["default"] = in.value("file", std::string());
    wsOut["group"]                                              = "ndmspc/browser";
    out["result"]                                               = "success";
  };
  handlers["ndmspc/browser/draw"] = [](std::string, json &, json & out, json & wsOut,
                                std::map<std::string, TObject *> &) {
    wsOut["payload"]["pad"] = json::array({json{{"pad", 0}, {"label", "h"}}});
    wsOut["group"]          = "ndmspc/browser";
    out["result"]           = "success";
  };

  // The base actions too: the session one is what names a session, so a room without it cannot be asked
  // for what this is about.
  Ndmspc::gNdmspcHttpHandlers = &handlers;
  Ndmspc::RegisterBaseActions();

  // A room is what has a session snapshot, and the room it is comes from the environment (see
  // NHttpServer's constructor), so this one says which room it is.
  setenv("NDMSPC_ROOM", "test-room", 1);
  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  unsetenv("NDMSPC_ROOM");
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  // The open is what a session of this group is, which is also what makes it replayable: the snapshot
  // replays the actions that define a session, so one that declares none leaves nothing to report.
  tools["ndmspc/browser/open"]          = {.order = 1, .session = true};
  tools["ndmspc/browser/draw"]          = {.dependsOn = {"ndmspc/browser/open"}};
  Ndmspc::gNdmspcMcpTools        = &tools;

  // A step that starts a session names it after its group, and drawing in it is remembered with it.
  const json opened = json::parse(RequestJson(server, "POST", "ndmspc/browser/open", {{"file", "a.root"}}));
  ASSERT_TRUE(opened["result"] == "success");
  const std::string session = opened["combination"]["id"].get<std::string>();

  const json named = json::parse(
      RequestJson(server, "PATCH", "ndmspc/session", {{"session", session}, {"name", "calibration"}}));
  const json drawn =
      json::parse(RequestJson(server, "POST", "ndmspc/browser/draw", {{"path", json::array({session})}}));
  ASSERT_TRUE(named["result"] == "success");
  ASSERT_TRUE(drawn["result"] == "success");
  ASSERT_TRUE(named["payload"]["sessions"][0]["label"] == "calibration");

  // The snapshot carries the tree, the name the room holds and what the session has drawn.
  const json snapshot = server->RoomSessionSnapshot();
  ASSERT_FALSE(snapshot.is_null());
  ASSERT_TRUE(snapshot.contains("combinations"));
  ASSERT_TRUE(snapshot.contains("sessionNames"));
  EXPECT_TRUE(snapshot["sessionNames"][session] == "calibration");
  EXPECT_TRUE(snapshot["pads"].contains(session));

  // A room that wakes takes the tree back and then the room's own session state - the order the restore
  // uses, and what makes the names still belong to the right sessions.
  auto * woken = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  Ndmspc::NInstanceTree(woken->GetCombinations()).Restore(snapshot["combinations"]);
  woken->AdoptSessionState(snapshot);

  const json sessions = woken->SessionList()["sessions"];
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_TRUE(sessions[0]["id"] == session);
  EXPECT_TRUE(sessions[0]["label"] == "calibration");
  EXPECT_TRUE(woken->GetPads().contains(session));

  Ndmspc::gNdmspcMcpTools    = previous;
  Ndmspc::gNdmspcHttpHandlers = nullptr;
  delete woken;
  delete server;
}

/**
 * A group's steps are its own: running one group's step again rolls back the steps that followed it
 * **there**, and leaves another group's alone.
 *
 * The keys are namespaced (`ndmspc/ngnt/open` beside `ndmspc/browser/open`), which is what makes the
 * group the part before the key's **last** slash. Read as the part before the first, every family of the
 * namespace would be one group - `ndmspc` - and this re-run would tear the browser's step down with it,
 * closing a file nobody asked it to touch.
 */
TEST(NHttpServerGroupTest, RerunningAStepRollsBackItsOwnGroupAndNoOther)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"]    = SuccessHandler;
  handlers["ndmspc/ngnt/reshape"] = SuccessHandler;
  handlers["ndmspc/browser/open"] = SuccessHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(std::move(handlers));

  ASSERT_NE(Request(server, "POST", "ndmspc/ngnt/open").find("success"), std::string::npos);
  ASSERT_NE(Request(server, "POST", "ndmspc/ngnt/reshape").find("success"), std::string::npos);
  ASSERT_NE(Request(server, "POST", "ndmspc/browser/open").find("success"), std::string::npos);
  // The analysis group starts over: its own later step goes, the browser's run is another group's.
  ASSERT_NE(Request(server, "POST", "ndmspc/ngnt/open").find("success"), std::string::npos);

  // What ran, in order: the root endpoint answers with the workspace history.
  const json history = json::parse(Request(server, "GET", ""))["state"]["history"];
  std::vector<std::string> names;
  for (const auto & entry : history) {
    names.push_back(entry["name"].get<std::string>());
  }

  EXPECT_EQ(std::find(names.begin(), names.end(), "ndmspc/ngnt/reshape"), names.end())
      << "the re-run analysis group kept its own rolled-back step";
  EXPECT_NE(std::find(names.begin(), names.end(), "ndmspc/browser/open"), names.end())
      << "the re-run analysis group took the browser's step with it";
  ASSERT_FALSE(names.empty());
  EXPECT_EQ(names.back(), "ndmspc/ngnt/open") << "the re-run is the newest step";

  delete server;
}
