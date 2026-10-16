#include <gtest/gtest.h>

#include "ndmspc/http/NBaseActions.h"
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NInstanceTree.h"

#include <THttpCallArg.h>
#include <TNamed.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
  // The room's drawings: GET reports them, DELETE forgets them, so a view that clears them does not
  // have them handed back on its next visit.
  EXPECT_NE(handlers.find("ndmspc/pads"), handlers.end());
  // A combination written out as a macro the user can run locally: a read, so a room's whole
  // combination is handed on without anything being run again.
  EXPECT_NE(handlers.find("ndmspc/macro"), handlers.end());
  EXPECT_NE(tools.find("ndmspc/health"), tools.end());
  EXPECT_NE(tools.find("ndmspc/state"), tools.end());
  EXPECT_NE(tools.find("ndmspc/group"), tools.end());
  EXPECT_NE(tools.find("ndmspc/session"), tools.end());
  EXPECT_NE(tools.find("ndmspc/heartbeat"), tools.end());
  EXPECT_NE(tools.find("ndmspc/pads"), tools.end());
  EXPECT_NE(tools.find("ndmspc/macro"), tools.end());

  // The debug echo helper is gone with the macro it used to live in: a macro that wants one
  // registers its own.
  EXPECT_EQ(handlers.find("debug"), handlers.end());
  EXPECT_EQ(tools.find("debug"), tools.end());

  // Registering twice is what a deployment that also loads the deprecated toolBase.C shim does.
  EXPECT_TRUE(Ndmspc::RegisterBaseActions());
  EXPECT_EQ(handlers.size(), 8u);

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

// A handler that blocks until the test releases it, so an action can be observed while it is in flight.
// Ndmspc::NHttpFuncPtr is a plain function pointer (no captures), so its state lives at namespace scope.
std::mutex              gBusyMutex;
std::condition_variable gBusyCv;
bool                    gBusyStarted = false;
bool                    gBusyRelease = false;

// What a tool-like handler's DELETE saw, and whether it found the object it looked for. File scope so
// the handlers can stay non-capturing (NHttpFuncPtr is a plain function pointer).
std::vector<std::string> gDeleteSessions;
bool                     gDeleteFound = false;

/**
 * A tool-like handler: it owns one object per session, keyed `<name>@<session>` - the rule
 * NRouteContext::ObjectName follows - creating it on POST and freeing it on DELETE. On DELETE it
 * records the session it ran as, so a replay that did not restore the entry's own session (and so
 * looked under the wrong key, freeing nothing) is caught.
 */
void SessionObjectHandler(const std::string & name, std::string method, json & /*in*/, json & out, json & wsOut,
                          std::map<std::string, TObject *> & objects)
{
  auto *            server  = Ndmspc::gNHttpServer;
  const std::string session = server == nullptr ? std::string() : server->GetCurrentSession();
  const std::string key     = session.empty() ? name : name + "@" + session;

  if (method.find("DELETE") != std::string::npos) {
    gDeleteSessions.push_back(session);
    if (objects.find(key) != objects.end()) {
      gDeleteFound = true;
      delete objects[key];
      objects.erase(key);
    }
    out["result"] = "success";
    return;
  }

  objects[key]  = new TNamed(name.c_str(), "");
  out["result"] = "success";
  // Draw something too, so the room records a pad for this session (see RecordPads).
  wsOut["payload"]["pad"] = json::array({json{{"pad", "pad1"}, {"label", name}}});
}

void BlockingHandler(std::string /*method*/, json & /*in*/, json & out, json & /*wsOut*/,
                     std::map<std::string, TObject *> & /*objects*/)
{
  {
    std::lock_guard<std::mutex> lock(gBusyMutex);
    gBusyStarted = true;
  }
  gBusyCv.notify_all();
  std::unique_lock<std::mutex> lock(gBusyMutex);
  gBusyCv.wait(lock, [] { return gBusyRelease; });
  out["result"] = "success";
}

/// @brief Resets the blocking handler's flags so a test starts from a clean state.
void ResetBlockingHandler()
{
  std::lock_guard<std::mutex> lock(gBusyMutex);
  gBusyStarted = false;
  gBusyRelease = false;
}

/// @brief Starts an action on `server` that blocks in {@link BlockingHandler}, and waits till it runs.
void StartBlockingAction(Ndmspc::NHttpServer * server, const char * requestId)
{
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod("POST");
  arg->SetPathName("api");
  arg->SetFileName("ndmspc/ngnt/reshape");
  arg->SetPostData(R"({"path":["i1","i2"]})");
  server->RunAction(arg, nullptr, requestId, nullptr);
  // Wait until the handler is actually running, so the in-flight list is populated before it is read.
  std::unique_lock<std::mutex> lock(gBusyMutex);
  gBusyCv.wait(lock, [] { return gBusyStarted; });
}

/// @brief Releases a {@link BlockingHandler} started by {@link StartBlockingAction}, and lets it finish.
void ReleaseBlockingAction()
{
  {
    std::lock_guard<std::mutex> lock(gBusyMutex);
    gBusyRelease = true;
  }
  gBusyCv.notify_all();
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

// The same, carrying the identity header an authenticating front door would set - what a server told
// to trust that door (SetTrustForwardedIdentity) reads as a verified identity, and so what the
// allowed-user gate sees.
std::string RequestAsUser(Ndmspc::NHttpServer * server, const char * method, const char * action,
                          const std::string & user)
{
  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod(method);
  arg->SetPathName("api");
  arg->SetFileName(action);
  arg->SetPostData("{}");
  arg->SetRequestHeader(("X-Ndmspc-User: " + user + "\r\n").c_str());
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

/// How many times each step of {@link ATransientClickCostsNoHistoryEntryAndNoRebuild} was run: a step that
/// is replayed to restore a chain is run a second time, which is exactly what that test is about.
int gOpenRuns = 0, gReshapeRuns = 0, gMapRuns = 0, gSpectraRuns = 0;

void CountStep(int & runs, std::string method, json & out)
{
  if (method == "POST") runs++;
  out["result"] = "success";
}

void CountOpen(std::string method, json &, json & out, json &, std::map<std::string, TObject *> &)
{
  CountStep(gOpenRuns, method, out);
}

void CountReshape(std::string method, json &, json & out, json &, std::map<std::string, TObject *> &)
{
  CountStep(gReshapeRuns, method, out);
}

void CountMap(std::string method, json &, json & out, json &, std::map<std::string, TObject *> &)
{
  CountStep(gMapRuns, method, out);
}

void CountSpectra(std::string method, json &, json & out, json &, std::map<std::string, TObject *> &)
{
  CountStep(gSpectraRuns, method, out);
}

/**
 * A click's **transient** POST is not a step: it records no history entry, and it must not cost the chain
 * it ran on anything. It used to do both — the entry it left made the group's entry count disagree with
 * its chain, and the next request tore the chain down and replayed it, re-opening the file and re-running
 * the reshape to serve a click on a histogram.
 */
TEST(NHttpServerCombinationTest, ATransientClickCostsNoHistoryEntryAndNoRebuild)
{
  gOpenRuns = gReshapeRuns = gMapRuns = gSpectraRuns = 0;

  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"]    = CountOpen;
  handlers["ndmspc/ngnt/reshape"] = CountReshape;
  handlers["ndmspc/ngnt/map"]     = CountMap;
  handlers["ndmspc/ngnt/spectra"] = CountSpectra;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  Ndmspc::NMcpToolMap   tools;
  tools["ndmspc/ngnt/open"]    = {};
  tools["ndmspc/ngnt/reshape"] = {.dependsOn = {"ndmspc/ngnt/open"}};
  tools["ndmspc/ngnt/map"]     = {.dependsOn = {"ndmspc/ngnt/reshape"}};
  tools["ndmspc/ngnt/spectra"] = {.dependsOn = {"ndmspc/ngnt/map"}};
  Ndmspc::gNdmspcMcpTools      = &tools;

  // The chain a user works: open the file, reshape it, map it.
  const json        opened   = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "a.root"}}));
  const std::string i1       = opened["combination"]["id"];
  const json        reshaped = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"binningName", "x"}}));
  const std::string i2       = reshaped["combination"]["id"];
  const json        mapped   = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/map", {}));
  const std::string i3       = mapped["combination"]["id"];
  ASSERT_EQ(gOpenRuns, 1);
  ASSERT_EQ(gReshapeRuns, 1);
  ASSERT_EQ(gMapRuns, 1);

  // A click on the map draws its spectra - a transient POST, which is not a step of the combination.
  RequestJson(server, "POST", "ndmspc/ngnt/spectra", {{"path", json::array({i1, i2, i3})}, {"transient", true}});
  ASSERT_EQ(gSpectraRuns, 1);

  // And the drill that follows it - a click on another cell, which PATCHes the map's own node - must find
  // the chain where it was: nothing re-opened, nothing re-reshaped, nothing re-mapped.
  RequestJson(server, "PATCH", "ndmspc/ngnt/map",
              {{"path", json::array({i1, i2, i3})}, {"point", json::array({1})}, {"level", 1}});
  EXPECT_EQ(gOpenRuns, 1) << "the file was opened again";
  EXPECT_EQ(gReshapeRuns, 1) << "the reshape was run again";
  EXPECT_EQ(gMapRuns, 1) << "the map was drawn again";
  EXPECT_EQ(gSpectraRuns, 1) << "the click's own drawing was replayed";
  EXPECT_EQ(server->GetCombinations()["active"], json::array({i1, i2, i3}));

  Ndmspc::gNdmspcMcpTools = previous;
  delete server;
}

/**
 * A combination written out as a macro (ndmspc/macro), and the promise that asking for one costs the
 * room nothing: no step is replayed, no node is created and no history entry is appended, so a room
 * hands its whole combination on without moving.
 */
TEST(NHttpServerMacroExportTest, WritesACombinationWithoutRunningOrRecordingAnything)
{
  Ndmspc::NHttpHandlerMap * previousHandlers = Ndmspc::gNdmspcHttpHandlers;
  Ndmspc::NMcpToolMap *     previousTools    = Ndmspc::gNdmspcMcpTools;
  std::map<std::string, Ndmspc::NHttpFuncPtr> all;
  Ndmspc::NMcpToolMap                         tools;
  Ndmspc::gNdmspcHttpHandlers = &all;
  Ndmspc::gNdmspcMcpTools      = &tools;
  all["ndmspc/ngnt/open"]      = SuccessHandler;
  all["ndmspc/ngnt/reshape"]   = SuccessHandler;
  // The export itself is a base action, registered the way a CLI registers it.
  ASSERT_TRUE(Ndmspc::RegisterBaseActions());

  // What the tools say their steps are in code - the only input the export has beyond the tree.
  tools["ndmspc/ngnt/open"]    = {.label         = "{{ file }}",
                                  .macroLines    = {"auto * ngnt = Ndmspc::NGnTree::Open({{ file }});"},
                                  .macroIncludes = {"ndmspc/core/NGnTree.h"}};
  tools["ndmspc/ngnt/reshape"] = {.dependsOn = {"ndmspc/ngnt/open"},
                                  .macroLines = {"auto * nav = ngnt->Reshape({{ binningName }}, {{ levels }});"}};

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, false);
  server->SetHttpHandlers(all);

  // A combination: one open, one reshape under it.
  const json        opened  = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "test.root"}}));
  const std::string i1      = opened["combination"]["id"];
  const json        reshaped = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/reshape",
                                                       {{"binningName", "default"},
                                                        {"levels", json::array({json::array({0, 1, 2})})}}));
  const std::string i2      = reshaped["combination"]["id"];

  const std::size_t historyBefore = server->GetJson().size();
  const std::size_t nodesBefore   = server->GetCombinations()["nodes"].size();

  // The macro is named after the session the room is on - what its own view calls that session - because
  // ROOT names a macro after the function in it, so the file has to follow the same identifier. A room names
  // a session as it starts one, and the first of a group is "ndmspc/ngnt 1"; reading that name back is what
  // this pins, since a name read out of a list that is gone is how this came out as a node id instead.
  const json exported = json::parse(Request(server, "GET", "ndmspc/macro"));
  ASSERT_EQ(exported["result"], "success") << exported.dump();

  EXPECT_EQ(exported["payload"]["name"], "ndmspc_ngnt_1.C");
  const std::string macro = exported["payload"]["macro"];
  EXPECT_NE(macro.find("void ndmspc_ngnt_1()"), std::string::npos) << macro;
  EXPECT_NE(macro.find("#include <ndmspc/core/NGnTree.h>"), std::string::npos) << macro;
  EXPECT_NE(macro.find("auto * ngnt = Ndmspc::NGnTree::Open(\"test.root\");"), std::string::npos) << macro;
  EXPECT_NE(macro.find("ngnt->Reshape(\"default\", {{0, 1, 2}});"), std::string::npos) << macro;

  // Nothing moved: the same history, the same nodes, the same active combination.
  EXPECT_EQ(server->GetJson().size(), historyBefore);
  EXPECT_EQ(server->GetCombinations()["nodes"].size(), nodesBefore);
  EXPECT_EQ(server->GetCombinations()["active"], json::array({i1, i2}));

  // A named combination is written instead of the active one - still without running anything.
  const json branch =
      json::parse(RequestJson(server, "GET", "ndmspc/macro", {{"combination", json::array({i1})}}));
  ASSERT_EQ(branch["result"], "success") << branch.dump();
  const std::string branchMacro = branch["payload"]["macro"].get<std::string>();
  EXPECT_NE(branchMacro.find("NGnTree::Open"), std::string::npos) << branchMacro;
  EXPECT_EQ(branchMacro.find("Reshape"), std::string::npos) << branchMacro;
  EXPECT_EQ(server->GetJson().size(), historyBefore);

  // A rename stands, and the macro follows it too: what a session is called names the macro, whether the room
  // handed the name out or a user gave it.
  ASSERT_TRUE(server->RenameSession(i1, "ngnt demo"));
  const json renamed = json::parse(Request(server, "GET", "ndmspc/macro"));
  ASSERT_EQ(renamed["result"], "success") << renamed.dump();
  EXPECT_EQ(renamed["payload"]["name"], "ngnt_demo.C");
  EXPECT_NE(renamed["payload"]["macro"].get<std::string>().find("void ngnt_demo()"), std::string::npos)
      << renamed["payload"]["macro"];
  EXPECT_EQ(server->GetJson().size(), historyBefore);

  // It only reads: a write is refused, and leaves no history entry behind either.
  const json refused = json::parse(Request(server, "POST", "ndmspc/macro"));
  EXPECT_EQ(refused["result"], "failure");
  EXPECT_NE(refused["error"].get<std::string>().find("only reads"), std::string::npos);
  EXPECT_EQ(server->GetJson().size(), historyBefore);

  // A room with nothing open has no combination to write, and says so rather than answering an empty
  // macro.
  auto * fresh  = new Ndmspc::NHttpServer("", true, 10000, {}, false);
  fresh->SetHttpHandlers(all);
  const json nothing = json::parse(Request(fresh, "GET", "ndmspc/macro"));
  EXPECT_EQ(nothing["result"], "failure");
  EXPECT_EQ(nothing["error"], "No combination to write: nothing has been opened in this room yet");

  Ndmspc::gNdmspcHttpHandlers = previousHandlers;
  Ndmspc::gNdmspcMcpTools      = previousTools;
  delete fresh;
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

/**
 * A session the room is already on keeps the chain it has live when it is activated again.
 *
 * A client reads a step as "on the server" from the group's live chain (`combinations.active`), and
 * re-entering a combination - which is what a run dialog does as it closes - used to set that chain back
 * to the bare session. Every step under it then read as un-run, so a combination that had already run
 * came back asking to be run again.
 */
TEST(NHttpServerGroupTest, ReEnteringTheSessionTheRoomIsOnKeepsItsLiveChain)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"]    = SuccessHandler;
  handlers["ndmspc/ngnt/reshape"] = SuccessHandler;

  // The base actions too: `ndmspc/session` is what re-enters a combination.
  Ndmspc::gNdmspcHttpHandlers = &handlers;
  Ndmspc::RegisterBaseActions();

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previousTools = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]    = {};
  tools["ndmspc/ngnt/reshape"] = {.dependsOn = {"ndmspc/ngnt/open"}};
  Ndmspc::gNdmspcMcpTools      = &tools;

  const json        open    = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "a.root"}}));
  const std::string i1      = open["combination"]["id"];
  const json        reshape = json::parse(
      RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"binningName", "x"}, {"path", json::array({i1})}}));
  const std::string i2 = reshape["combination"]["id"];

  Ndmspc::NInstanceTree tree(server->GetCombinations());
  ASSERT_EQ(tree.Active("ndmspc/ngnt"), std::vector<std::string>({i1, i2}));

  // Re-entering the session the room is on: what a run dialog does as it closes.
  const json again = json::parse(RequestJson(server, "PATCH", "ndmspc/session", {{"session", i1}}));
  EXPECT_TRUE(again.value("result", std::string()) == "success") << again.dump();

  EXPECT_EQ(tree.Active("ndmspc/ngnt"), std::vector<std::string>({i1, i2}))
      << "re-entering the session the room is on threw its live chain away";
  EXPECT_EQ(tree.Active(), std::vector<std::string>({i1, i2}));

  Ndmspc::gNdmspcMcpTools     = previousTools;
  Ndmspc::gNdmspcHttpHandlers = nullptr;
  delete server;
}

TEST(NHttpServerCombinationTest, ClearingASessionFreesItsOwnObjects)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  // A tool-like pair: each step owns one object, keyed by the session the request runs in (the same
  // rule NRouteContext::ObjectName follows), created on POST and freed on DELETE.
  handlers["ndmspc/ngnt/open"] = [](std::string method, json & in, json & out, json & wsOut,
                                    std::map<std::string, TObject *> & objects) {
    SessionObjectHandler("ngnt", method, in, out, wsOut, objects);
  };
  handlers["ndmspc/ngnt/reshape"] = [](std::string method, json & in, json & out, json & wsOut,
                                       std::map<std::string, TObject *> & objects) {
    SessionObjectHandler("navigator", method, in, out, wsOut, objects);
  };

  Ndmspc::gNdmspcHttpHandlers = &handlers;
  Ndmspc::RegisterBaseActions();

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previousTools = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]    = {};
  tools["ndmspc/ngnt/reshape"] = {.dependsOn = {"ndmspc/ngnt/open"}};
  Ndmspc::gNdmspcMcpTools      = &tools;

  const json        open = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "a.root"}}));
  const std::string id   = open["combination"]["id"];
  ASSERT_FALSE(id.empty()) << open.dump();
  RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"binningName", "x"}, {"path", json::array({id})}});
  ASSERT_EQ(server->GetObjectsMap().count("ngnt@" + id), 1u);
  ASSERT_EQ(server->GetObjectsMap().count("navigator@" + id), 1u);
  ASSERT_TRUE(server->GetPads().contains(id)) << "the drawing was not recorded for the session";

  // Clearing the session: a DELETE on its root node, the way the room's Clear session asks for it.
  const json cleared = json::parse(RequestJson(server, "DELETE", "ndmspc/ngnt/open", {{"path", json::array({id})}}));
  EXPECT_TRUE(cleared.value("result", std::string()) == "success") << cleared.dump();

  // Every step's objects are gone: the root's DELETE runs once, and each step under it is torn down
  // through the subtree (RemoveSubtree's callback), not left to the route-name rollback.
  EXPECT_EQ(server->GetObjectsMap().count("ngnt@" + id), 0u) << "the session's file object was left behind";
  EXPECT_EQ(server->GetObjectsMap().count("navigator@" + id), 0u) << "a step's object was left behind";
  // And its drawings, which are the serialized objects (megabytes) and nothing else would drop: they
  // are what a room that cleared a session would otherwise go on serving and reporting for ever.
  EXPECT_FALSE(server->GetPads().contains(id)) << "the cleared session's drawings were left behind";

  Ndmspc::gNdmspcMcpTools     = previousTools;
  Ndmspc::gNdmspcHttpHandlers = nullptr;
  delete server;
}

TEST(NHttpServerCombinationTest, AResetReplaysEachStepsDeleteAsItsOwnSession)
{
  gDeleteSessions.clear();
  gDeleteFound = false;

  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/open"] = [](std::string method, json & in, json & out, json & wsOut,
                                    std::map<std::string, TObject *> & objects) {
    SessionObjectHandler("ngnt", method, in, out, wsOut, objects);
  };
  handlers["ndmspc/ngnt/reshape"] = [](std::string method, json & in, json & out, json & wsOut,
                                       std::map<std::string, TObject *> & objects) {
    SessionObjectHandler("navigator", method, in, out, wsOut, objects);
  };

  Ndmspc::gNdmspcHttpHandlers = &handlers;
  Ndmspc::RegisterBaseActions();

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previousTools = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]    = {};
  tools["ndmspc/ngnt/reshape"] = {.dependsOn = {"ndmspc/ngnt/open"}};
  Ndmspc::gNdmspcMcpTools      = &tools;

  const json        open = json::parse(RequestJson(server, "POST", "ndmspc/ngnt/open", {{"file", "a.root"}}));
  const std::string id   = open["combination"]["id"];
  ASSERT_FALSE(id.empty()) << open.dump();
  RequestJson(server, "POST", "ndmspc/ngnt/reshape", {{"binningName", "x"}, {"path", json::array({id})}});
  ASSERT_EQ(server->GetObjectsMap().count("ngnt@" + id), 1u);

  // A reset is what a room is told to do when its session is forgotten: a DELETE on the state control,
  // which is not a node action, so the request itself sets no session - every replayed DELETE has to
  // take the session from its entry. Run as none, a handler looks under a bare key and frees nothing.
  const json reset = json::parse(RequestJson(server, "DELETE", "ndmspc/state", json::object()));
  EXPECT_TRUE(reset.value("result", std::string()) == "success") << reset.dump();

  ASSERT_FALSE(gDeleteSessions.empty()) << "the reset replayed no step's DELETE";
  for (const auto & session : gDeleteSessions) EXPECT_EQ(session, id) << "a replayed DELETE ran as the wrong session";
  EXPECT_TRUE(gDeleteFound) << "a replayed handler never found its object: it ran as the wrong session";
  EXPECT_TRUE(server->GetObjectsMap().empty()) << "objects were left behind after a reset";
  EXPECT_TRUE(server->GetPads().empty()) << "drawings were left behind after a reset";

  Ndmspc::gNdmspcMcpTools     = previousTools;
  Ndmspc::gNdmspcHttpHandlers = nullptr;
  delete server;
}

TEST(NHttpServerAccessTest, ANonListedUserIsNotRefusedInGuestMode)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", "alice");
  EnvGuard mode("NDMSPC_ALLOWED_USERS_MODE", nullptr); // default: guest
  auto *   server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetTrustForwardedIdentity(true);

  // A guest is let through here (and held read-only elsewhere), not refused at the entry.
  const std::string response = RequestAsUser(server, "GET", "ndmspc/state", "bob");
  EXPECT_EQ(response.find("user_not_allowed"), std::string::npos) << response;

  delete server;
}

TEST(NHttpServerAccessTest, ANonListedUserIsRefusedInDenyMode)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", "alice");
  EnvGuard mode("NDMSPC_ALLOWED_USERS_MODE", "deny");
  auto *   server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetTrustForwardedIdentity(true);

  const std::string refused = RequestAsUser(server, "GET", "ndmspc/state", "bob");
  EXPECT_NE(refused.find("\"code\":\"user_not_allowed\""), std::string::npos) << refused;
  EXPECT_NE(refused.find(Ndmspc::NAccessPolicy::DefaultMessage()), std::string::npos) << refused;

  delete server;
}

TEST(NHttpServerAccessTest, AListedUserPassesTheGateAnyIdentifier)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", "alice");
  auto *   server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetTrustForwardedIdentity(true);

  // The list holds a user name; the request names the same person by their user name and by their
  // email (case-insensitively) - both are "alice" and both are admitted. The handler is not registered
  // here, so the answer is the "unsupported action" one - anything but the refusal is the proof.
  for (const std::string & user : {std::string("alice"), std::string("ALICE")}) {
    const std::string response = RequestAsUser(server, "GET", "ndmspc/state", user);
    EXPECT_EQ(response.find("user_not_allowed"), std::string::npos) << response;
  }

  delete server;
}

TEST(NHttpServerAccessTest, WithNoAllowListEveryVerifiedUserPasses)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", nullptr);
  auto *   server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetTrustForwardedIdentity(true);

  const std::string response = RequestAsUser(server, "GET", "ndmspc/state", "bob");
  EXPECT_EQ(response.find("user_not_allowed"), std::string::npos) << response;

  delete server;
}

TEST(NHttpServerAccessTest, AnAnonymousRequestIsNeverGatedByTheList)
{
  EnvGuard users("NDMSPC_ALLOWED_USERS", "alice");
  auto *   server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  // No verified identity reaches the funnel (the door's headers are not believed here), so the list -
  // which is an "after authentication" gate - does not apply and nothing is refused.
  const std::string response = RequestAsUser(server, "GET", "ndmspc/state", "");
  EXPECT_EQ(response.find("user_not_allowed"), std::string::npos) << response;

  delete server;
}

TEST(NHttpServerBusyTest, AnActionInFlightIsListedWhileItRuns)
{
  ResetBlockingHandler();
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/reshape"] = BlockingHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  StartBlockingAction(server, "req-42");

  // While it runs: one entry, named, with the combination it acts on and its age; the count agrees.
  const json list = server->BusyActionsList();
  ASSERT_EQ(list.size(), 1u) << list.dump();
  EXPECT_EQ(list[0]["id"].get<std::string>(), "req-42");
  EXPECT_EQ(list[0]["action"].get<std::string>(), "ndmspc/ngnt/reshape");
  EXPECT_EQ(list[0]["path"], json::array({"i1", "i2"}));
  EXPECT_EQ(list[0]["method"].get<std::string>(), "POST");
  EXPECT_GE(list[0]["elapsedMs"].get<long long>(), 0);
  EXPECT_EQ(server->BusyActions(), 1);

  ReleaseBlockingAction();
  // Poll until the worker threads the action (and its list entry) away.
  for (int i = 0; i < 200 && !server->BusyActionsList().empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  EXPECT_TRUE(server->BusyActionsList().empty());
  EXPECT_EQ(server->BusyActions(), 0);

  delete server;
}

TEST(NHttpServerBusyTest, TheKillSwitchTurnsTheListOff)
{
  EnvGuard off("NDMSPC_BUSY_BROADCAST", "0");
  ResetBlockingHandler();
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  handlers["ndmspc/ngnt/reshape"] = BlockingHandler;

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  StartBlockingAction(server, "req-43");
  // Switched off: nothing recorded, so no list - but the count (the chip, the cadence) is unchanged.
  EXPECT_TRUE(server->BusyActionsList().empty());
  EXPECT_EQ(server->BusyActions(), 1);

  ReleaseBlockingAction();
  delete server;
}

TEST(NHttpServerBusyTest, TheStateReadCarriesTheBusyList)
{
  std::map<std::string, Ndmspc::NHttpFuncPtr> handlers;
  Ndmspc::NMcpToolMap                         tools;
  Ndmspc::NHttpHandlerMap *                   previousHandlers = Ndmspc::gNdmspcHttpHandlers;
  Ndmspc::NMcpToolMap *                       previousTools    = Ndmspc::gNdmspcMcpTools;
  Ndmspc::gNdmspcHttpHandlers = &handlers;
  Ndmspc::gNdmspcMcpTools      = &tools;
  ASSERT_TRUE(Ndmspc::RegisterBaseActions());

  auto * server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);
  server->SetHttpHandlers(handlers);

  const json body = json::parse(Request(server, "GET", "ndmspc/state"));
  Ndmspc::gNdmspcHttpHandlers = previousHandlers;
  Ndmspc::gNdmspcMcpTools      = previousTools;
  delete server;

  ASSERT_TRUE(body.contains("payload")) << body.dump();
  const json & state = body["payload"]["state"];
  ASSERT_TRUE(state.contains("busy")) << body.dump();
  EXPECT_TRUE(state["busy"].is_array());
  EXPECT_TRUE(state["busy"].empty());
}

TEST(NHttpServerBusyTest, ACancelIsRefusedForAReadOnlyToken)
{
  // A room that enforces access, so the control request has a level to check.
  EnvGuard access("NDMSPC_ROOM_ACCESS", R"({"rw":"rwTok","ro":"roTok"})");
  auto *   server = new Ndmspc::NHttpServer("", true, 10000, {}, /*startEngine=*/false);

  auto arg = std::make_shared<THttpCallArg>();
  arg->SetMethod("POST");
  arg->SetPathName("api");
  arg->SetFileName("cancel");
  arg->SetQuery("token=roTok");
  arg->SetPostData(R"({"requestId":"whatever"})");
  server->ProcessRequest(arg);

  const std::string body(static_cast<const char *>(arg->GetContent()), arg->GetContentLength());
  delete server;
  EXPECT_NE(body.find("read-only"), std::string::npos) << body;
  EXPECT_EQ(body.find("\"cancelled\":true"), std::string::npos) << body;
}
