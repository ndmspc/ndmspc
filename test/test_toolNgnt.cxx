/**
 * The ngnt tool, loaded and called the way the server loads and calls it.
 *
 * What this covers is the tool's **registration** and its handler surface — the macro loads, its entry
 * function registers the chain with its metadata, and the handlers the server would route to exist — and the
 * chain itself, driven the way the room drives it (see `Dispatch`): a map run records the layer it stands on,
 * which is what the line it hands on walks to. A direct handler call would not do: the chain leans on the
 * server's per-request plumbing (the node materialization and the workspace state), which a request to `/api`
 * goes through and a bare call does not.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <TAxis.h>
#include <TH1.h>
#include <THttpCallArg.h>
#include <TList.h>
#include <TObjArray.h>

#include "05_single_bining/NSingleBinning01Gaus.C"

#include "ndmspc/core/NBinning.h"
#include "ndmspc/core/NGnTree.h"
#include "ndmspc/http/NInstanceTree.h"
#include "ndmspc/http/NMacroBuilder.h"

#include "NToolHarness.h"

namespace {

using Ndmspc::Test::ToolHarness;

/// The macro under test, resolved by the test build.
constexpr const char * kToolMacro = NDMSPC_TOOL_MACRO;

/// Sets an environment variable for as long as the object lives, and puts the old one back after.
class ScopedEnvVar {
  public:
  ScopedEnvVar(std::string name, std::string value) : fName(std::move(name))
  {
    const char * previous = std::getenv(fName.c_str());
    if (previous) {
      fHadPrevious = true;
      fPrevious    = previous;
    }
    setenv(fName.c_str(), value.c_str(), 1);
  }
  ~ScopedEnvVar()
  {
    if (fHadPrevious) setenv(fName.c_str(), fPrevious.c_str(), 1);
    else unsetenv(fName.c_str());
  }
  ScopedEnvVar(const ScopedEnvVar &)            = delete;
  ScopedEnvVar & operator=(const ScopedEnvVar &) = delete;

  private:
  std::string fName;
  std::string fPrevious;
  bool        fHadPrevious{false};
};

/// The chain as the tool declares it: each action, what it depends on, and where it sits in order.
struct ToolAction {
  const char * key;
  const char * dependsOn;
  const char * label;
  int          order;
};

const ToolAction kChain[] = {
    {"ndmspc/ngnt/open", "", "{{ file }}", 1},
    {"ndmspc/ngnt/reshape", "ndmspc/ngnt/open", "{{ binningName }} ({{ levels }})", 2},
    {"ndmspc/ngnt/map", "ndmspc/ngnt/reshape", "{{ mappingPad }}", 3},
    {"ndmspc/ngnt/spectra", "ndmspc/ngnt/map", "{{ parameter }}", 4},
    {"ndmspc/ngnt/point", "ndmspc/ngnt/map", "", 5},
};

/// The file the map test opens: the tutorial's own example, written when it is not there yet.
const char * kNgntFile = "test_toolNgnt_single.root";

/// The file the no-parameter test opens: a tree with an axis but no parameters (a plain tree, opened
/// without `InitParameters`), so the spectra step has nothing to draw for.
const char * kNoParamNgntFile = "test_toolNgnt_noparams.root";

/// Write a minimal NGnTree with one axis and **no** parameters: what the workflow tour has to cope with.
void WriteNoParameterNgnt(const char * file)
{
  TObjArray * axes = new TObjArray();
  TAxis *     axis = new TAxis(5, 0, 5);
  axis->SetName("x");
  axis->SetTitle("X");
  axes->Add(axis);
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, file);
  delete axes;

  std::map<std::string, std::vector<std::vector<int>>> binning;
  binning["x"] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", binning);

  Ndmspc::NGnProcessFuncPtr func = [](Ndmspc::NBinningPoint * /*point*/, TList * /*output*/, TList * outputPoint,
                                      int /*threadId*/) { outputPoint->Add(new TH1F("h", "h", 10, 0, 10)); };
  ngnt->Process(func, json::object(), "default");
  delete ngnt;
}

/// A tool under test: its macro, loaded, with the handlers it registered.
class ToolNgnt : public ::testing::Test {
  protected:
  void SetUp() override { fTool = std::make_unique<ToolHarness>(kToolMacro); }

  /** One request through the same dispatch a call to `/api` uses — what records a combination node. */
  static json Dispatch(Ndmspc::NHttpServer * server, const char * method, const char * action, const json & body)
  {
    auto arg = std::make_shared<THttpCallArg>();
    arg->SetMethod(method);
    arg->SetPathName("api");
    arg->SetFileName(action);
    arg->SetPostData(body.dump());
    server->ProcessRequest(arg);
    return json::parse(std::string(static_cast<const char *>(arg->GetContent()), arg->GetContentLength()));
  }

  std::unique_ptr<ToolHarness> fTool;
};

TEST_F(ToolNgnt, LoadingTheMacroRegistersEveryActionOfTheChain)
{
  for (const ToolAction & action : kChain) {
    const Ndmspc::NHttpFuncPtr handler = fTool->Handler(action.key);
    EXPECT_NE(handler, nullptr) << action.key << " did not register a handler";
  }
  EXPECT_EQ(fTool->Handler("ndmspc/ngnt/nonexistent"), nullptr);
}

TEST_F(ToolNgnt, TheChainIsPublishedWithItsDependenciesOrderAndLabels)
{
  for (const ToolAction & action : kChain) {
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(action.key);
    ASSERT_NE(info, nullptr) << action.key << " registered no metadata";

    EXPECT_EQ(info->order, action.order) << action.key << " is out of order";
    EXPECT_EQ(info->label, action.label) << action.key << " labels its node differently";
    if (action.dependsOn[0] != '\0') {
      ASSERT_EQ(info->dependsOn.size(), 1u) << action.key << " should depend on one action";
      EXPECT_EQ(info->dependsOn[0], action.dependsOn);
    }
    else {
      EXPECT_TRUE(info->dependsOn.empty()) << action.key << " is the head of the chain";
    }
  }
}

TEST_F(ToolNgnt, AnActionCarriesAnInputSchemaSoAClientCanFillItIn)
{
  for (const ToolAction & action : kChain) {
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(action.key);
    ASSERT_NE(info, nullptr) << action.key;
    EXPECT_TRUE(info->inputSchema.contains("properties")) << action.key << " has no properties";
  }
}

/// The tour of the whole workflow is carried by the last action of the chain, and only by it: the client
/// offers it once, in the group's Help, and its steps name the actions they open themselves.
TEST_F(ToolNgnt, TheWorkflowTourHangsOffTheEndOfTheChain)
{
  const Ndmspc::NMcpToolInfo * spectra = fTool->ToolInfo("ndmspc/ngnt/spectra");
  ASSERT_NE(spectra, nullptr);
  ASSERT_TRUE(spectra->tutorial.is_object()) << "spectra carries no tour";
  EXPECT_EQ(spectra->tutorial.value("name", std::string()), "The NGNT workflow");
  ASSERT_TRUE(spectra->tutorial.contains("steps"));
  const json & steps = spectra->tutorial["steps"];
  ASSERT_TRUE(steps.is_array());
  ASSERT_FALSE(steps.empty());
  // The tour opens the session lead-in first, as the browser's tours do.
  EXPECT_EQ(steps[0].value("show", std::string()), "group");

  for (const ToolAction & action : kChain) {
    if (std::string(action.key) == "ndmspc/ngnt/spectra") continue;
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(action.key);
    ASSERT_NE(info, nullptr) << action.key;
    EXPECT_FALSE(info->tutorial.is_object()) << action.key << " should carry no tour of its own";
  }
}

/// What a step *is* in C++ is declared by the tool that owns it, and read by the macro export
/// (`ndmspc/macro`): a combination is handed on as the code that makes its objects, not as a script that
/// uses them — somebody takes the macro and goes on in ROOT from what it leaves behind. A step with no
/// recipe is not a gap in the macro (it reads as a comment), and a step that declares itself silent writes
/// nothing at all — so this also pins which steps carry what today.
TEST_F(ToolNgnt, EveryStepThatKnowsItsCodeDeclaresIt)
{
  // The layer a step is on, picked by the point the room recorded (`state.point`): `GetNavigator` is the walk
  // the room itself makes, so a navigator with several levels hands on the projection/spectra of the *selected*
  // one. An absent point gives `{}`, which reaches the navigator itself — the one-level case.
  const std::vector<std::pair<std::string, std::vector<std::string>>> declared = {
      {"ndmspc/ngnt/open", {"auto * ngnt = Ndmspc::NGnTree::Open({{ file }});"}},
      {"ndmspc/ngnt/reshape", {"auto * nav = ngnt->Reshape({{ binningName }}, {{ levels }});"}},
      {"ndmspc/ngnt/map", {"auto * projection = nav->GetNavigator({{ state.point }})->GetProjection();"}},
      {"ndmspc/ngnt/spectra",
       {"auto * spectra = nav->GetNavigator({{ state.point }})->DrawSpectraAll({{ parameter }}, { {{ axismargin }} }, {{ minmaxMode }});"}},
  };
  for (const auto & [key, lines] : declared) {
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(key);
    ASSERT_NE(info, nullptr) << key;
    EXPECT_EQ(info->macroLines, lines) << key << " writes its step differently";
    // Nothing the macro declares may *use* the objects it makes: it hands them over, and the user draws,
    // renders or projects as they please.
    for (const auto & line : info->macroLines) {
      EXPECT_EQ(line.find("->Draw("), std::string::npos) << key << " draws in the macro";
    }
  }

  // The point is the layer the room is on and makes nothing of its own: it says so, so the export neither
  // names it in a comment nor writes a line that would have to guess at an object.
  const Ndmspc::NMcpToolInfo * point = fTool->ToolInfo("ndmspc/ngnt/point");
  ASSERT_NE(point, nullptr);
  EXPECT_TRUE(point->macroSilent);
  EXPECT_TRUE(point->macroLines.empty());
  // A helper the drill calls on the room's behalf, not a step a person creates: it is published and
  // callable, but nothing offers it as a next step.
  EXPECT_FALSE(point->explorer);

  // The includes belong to the tool whose steps need them, and only to it.
  ASSERT_NE(fTool->ToolInfo("ndmspc/ngnt/open"), nullptr);
  ASSERT_NE(fTool->ToolInfo("ndmspc/ngnt/reshape"), nullptr);
  EXPECT_EQ(fTool->ToolInfo("ndmspc/ngnt/open")->macroIncludes, std::vector<std::string>({"ndmspc/core/NGnTree.h"}));
  EXPECT_EQ(fTool->ToolInfo("ndmspc/ngnt/reshape")->macroIncludes,
            std::vector<std::string>({"ndmspc/core/NGnNavigator.h"}));
  EXPECT_TRUE(fTool->ToolInfo("ndmspc/ngnt/map")->macroIncludes.empty()); // the navigator's own projection
}

TEST_F(ToolNgnt, TheChainReadsAsARunnableMacro)
{
  const json levels = json::array({json::array({0}), json::array({1, 2})});

  const Ndmspc::NMacroDocument doc = Ndmspc::BuildCombinationMacro(
      {{"ndmspc/ngnt/open", {{"file", "test.root"}}, json::object()},
       {"ndmspc/ngnt/reshape", {{"binningName", "default"}, {"levels", levels}}, json::object()},
       {"ndmspc/ngnt/map", json::object(), {{"point", json::array({1, 2})}}},
       {"ndmspc/ngnt/point", {{"entry", 3}}, {{"point", json::array({1, 2})}}},
       {"ndmspc/ngnt/spectra",
        {{"parameter", "mean"}, {"axismargin", 1.0}, {"minmaxMode", "V"}},
        {{"point", json::array({1, 2})}}}},
      "ngnt 1");

  EXPECT_EQ(doc.name, "ngnt_1");
  EXPECT_EQ(doc.text, "// ngnt 1: the room's active combination\n"
                      "// Written read-only - nothing in the room was changed.\n"
                      "\n"
                      "#include <ndmspc/core/NGnNavigator.h>\n"
                      "#include <ndmspc/core/NGnTree.h>\n"
                      "\n"
                      "void ngnt_1()\n"
                      "{\n"
                      "  auto * ngnt = Ndmspc::NGnTree::Open(\"test.root\");\n"
                      "  auto * nav = ngnt->Reshape(\"default\", {{0}, {1, 2}});\n"
                      "  auto * projection = nav->GetNavigator({1, 2})->GetProjection();\n"
                      "  auto * spectra = nav->GetNavigator({1, 2})->DrawSpectraAll(\"mean\", { 1.0 }, \"V\");\n"
                      "}\n");
}

/**
 * The map's first histogram carries the navigator's nested export (`children` per deeper level), which
 * the old `httpNgnt` map attached as `listJson["nested"]`. The new layered render must not lose it.
 */
TEST_F(ToolNgnt, AMapSendsTheNestedNavigatorExport)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNgntFile}}).reply["result"], "success");
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"},
                         {"levels", json::array({json::array({0}), json::array({1, 2})})}})
                .reply["result"],
            "success");

  const Ndmspc::Test::ToolCall mapped = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(mapped.reply["result"], "success") << mapped.reply.dump();

  const json & pads = mapped.ws["payload"]["pad"];
  ASSERT_TRUE(pads.is_array()) << mapped.ws.dump();
  ASSERT_FALSE(pads.empty()) << mapped.ws.dump();
  // The mapping histogram is the first layer; it carries the navigator export the old map attached
  // as `nested`, with one entry per deeper level.
  const json & value = pads[0]["value"];
  ASSERT_TRUE(value.contains("nested")) << mapped.ws.dump();
  EXPECT_TRUE(value["nested"].contains("children")) << value["nested"].dump();
}

/**
 * A map run records the layer it stands on, which is what the line it hands on walks to.
 *
 * The line is `nav->GetNavigator({{ state.point }})->GetProjection()` — a walk to the layer the step is on.
 * With nothing drilled that layer **is** the navigator, and a point the room never recorded renders as nothing
 * at all: `nav->GetNavigator()->GetProjection()`, which does not compile. So a run records the empty point,
 * and what the macro says is what the room drew.
 */
TEST_F(ToolNgnt, AMapRunRecordsTheLayerItStandsOn)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  Ndmspc::NHttpServer * server = fTool->Server();

  const json opened = Dispatch(server, "POST", "ndmspc/ngnt/open", {{"file", kNgntFile}});
  ASSERT_EQ(opened["result"], "success") << opened.dump();
  const std::string openId = opened["combination"]["id"];

  const json reshaped = Dispatch(server, "POST", "ndmspc/ngnt/reshape",
                                 {{"binningName", "default"},
                                  {"levels", json::array({json::array({0}), json::array({1, 2})})},
                                  {"path", json::array({openId})}});
  ASSERT_EQ(reshaped["result"], "success") << reshaped.dump();
  const std::string reshapeId = reshaped["combination"]["id"];

  const json mapped =
      Dispatch(server, "POST", "ndmspc/ngnt/map", {{"path", json::array({openId, reshapeId})}});
  ASSERT_EQ(mapped["result"], "success") << mapped.dump();
  const std::string mapId = mapped["combination"]["id"];

  // The layer it is on: empty, because the navigator it drew is the navigator itself.
  Ndmspc::NInstanceTree tree(server->GetCombinations());
  ASSERT_TRUE(tree.State(mapId).is_object()) << tree.ToTree().dump();
  EXPECT_EQ(tree.State(mapId)["point"], json::array());

  // And that is what the macro walks to: `GetNavigator({})` reaches the same navigator, so the line is code
  // that compiles and the step handed on is the one the room drew.
  const Ndmspc::NMacroDocument doc = Ndmspc::BuildCombinationMacro(
      {{tree.Action(openId), tree.Params(openId), tree.State(openId)},
       {tree.Action(reshapeId), tree.Params(reshapeId), tree.State(reshapeId)},
       {tree.Action(mapId), tree.Params(mapId), tree.State(mapId)}},
      "ngnt 1");
  EXPECT_NE(doc.text.find("  auto * projection = nav->GetNavigator({})->GetProjection();\n"), std::string::npos)
      << doc.text;
  EXPECT_EQ(doc.text.find("GetNavigator()"), std::string::npos) << doc.text;
}

/**
 * A tree with no parameters ends the workflow at the map: spectra has nothing to draw for, so the tour is
 * republished on open without the spectra step — the map is the last step, with no Save & Run after it.
 */
TEST_F(ToolNgnt, NoParametersEndsTheWorkflowTourAtTheMap)
{
  WriteNoParameterNgnt(kNoParamNgntFile);

  Ndmspc::NHttpServer * server = fTool->Server();
  const json            opened = Dispatch(server, "POST", "ndmspc/ngnt/open", {{"file", kNoParamNgntFile}});
  ASSERT_EQ(opened["result"], "success") << opened.dump();

  // The tour rides on the last step the chain offers: with no spectra to draw for, that is the map.
  const Ndmspc::NMcpToolInfo * spectra = fTool->ToolInfo("ndmspc/ngnt/spectra");
  ASSERT_NE(spectra, nullptr);
  const Ndmspc::NMcpToolInfo * map = fTool->ToolInfo("ndmspc/ngnt/map");
  ASSERT_NE(map, nullptr) << "the map carries the workflow tour when spectra is not offered";
  ASSERT_TRUE(map->tutorial.is_object());
  ASSERT_TRUE(map->tutorial.contains("steps"));
  const json & steps = map->tutorial["steps"];
  ASSERT_TRUE(steps.is_array());
  ASSERT_FALSE(steps.empty());

  const std::string lastShow = steps.back().value("show", std::string());
  EXPECT_NE(lastShow, "saveAndRun") << "the tour must not end on a Save & Run: " << steps.dump();

  bool sawMap = false;
  for (const auto & step : steps) {
    const std::string action = step.value("action", std::string());
    EXPECT_NE(action, "ndmspc/ngnt/spectra") << "the tour still opens the spectra step: " << steps.dump();
    if (action == "ndmspc/ngnt/map") sawMap = true;
  }
  EXPECT_TRUE(sawMap) << "the tour must still reach the map: " << steps.dump();
}

/**
 * A tree with no parameters leaves no spectra step at all: the map publishes no spectra schema, so the
 * step has nothing to offer (rather than an empty parameter list).
 */
TEST_F(ToolNgnt, NoParametersPublishesNoSpectraStep)
{
  WriteNoParameterNgnt(kNoParamNgntFile);

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNoParamNgntFile}}).reply["result"], "success");
  // The tool is not offered to a person either: the client's next-step list drops an `explorer:false`
  // tool, so the step is gone rather than shown with an empty list.
  const Ndmspc::NMcpToolInfo * spectra = fTool->ToolInfo("ndmspc/ngnt/spectra");
  ASSERT_NE(spectra, nullptr);
  EXPECT_FALSE(spectra->explorer) << "spectra must not be offered for a tree with no parameters";
  // And the tour moves to the step the chain does offer: a tour left on a step nobody is offered is a tour
  // nobody is shown (the Explorer reads the tours out of the tools it lists).
  const Ndmspc::NMcpToolInfo * map = fTool->ToolInfo("ndmspc/ngnt/map");
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(map->tutorial.contains("steps")) << "the map carries the tour when spectra is not offered";
  EXPECT_FALSE(spectra->tutorial.contains("steps")) << "spectra keeps no tour it cannot offer";
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"}, {"levels", json::array({json::array({0})})}})
                .reply["result"],
            "success");
  const Ndmspc::Test::ToolCall mapped = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(mapped.reply["result"], "success") << mapped.reply.dump();

  const json workspace = mapped.ws.contains("workspace") ? mapped.ws["workspace"] : json::object();
  EXPECT_FALSE(workspace.contains("spectra")) << "no parameters means no spectra step: " << workspace.dump();
}

/** The other side: a tree with parameters keeps the spectra step, with its parameters to pick from. */
TEST_F(ToolNgnt, ParametersKeepTheSpectraStep)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNgntFile}}).reply["result"], "success");
  EXPECT_TRUE(fTool->ToolInfo("ndmspc/ngnt/spectra")->explorer) << "spectra stays offered for a tree with parameters";
  // The tour rides on the last step the chain offers: with parameters to draw for, that is the spectra step.
  EXPECT_TRUE(fTool->ToolInfo("ndmspc/ngnt/spectra")->tutorial.contains("steps"));
  EXPECT_FALSE(fTool->ToolInfo("ndmspc/ngnt/map")->tutorial.contains("steps"))
      << "the map hands the tour to the step above it when there is one";
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"},
                         {"levels", json::array({json::array({0}), json::array({1, 2})})}})
                .reply["result"],
            "success");
  const Ndmspc::Test::ToolCall mapped = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(mapped.reply["result"], "success") << mapped.reply.dump();

  const json workspace = mapped.ws.contains("workspace") ? mapped.ws["workspace"] : json::object();
  ASSERT_TRUE(workspace.contains("spectra")) << workspace.dump();
  EXPECT_FALSE(workspace["spectra"]["properties"]["parameters"]["items"]["enum"].empty())
      << workspace["spectra"].dump();
}

/**
 * The reshape's `levels` name the file's axes, so a level is built by picking an axis rather than by
 * counting them out: the levels editor reads `axisLabels` for its Multi / Names / IDs switch.
 */
TEST_F(ToolNgnt, TheReshapeLevelsNameTheirAxes)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  const auto call = fTool->Call("ndmspc/ngnt/open", "POST", json{{"file", kNgntFile}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success") << call.reply.dump();

  const json & levels = call.ws["workspace"]["reshape"]["properties"]["levels"];
  ASSERT_TRUE(levels.contains("axisLabels")) << levels.dump();
  ASSERT_GE(levels["axisLabels"].size(), 1u);
  EXPECT_NE(levels["axisLabels"][0].get<std::string>().find("[0] mean"), std::string::npos)
      << levels["axisLabels"].dump();
  // A chosen axis is said by its own name, and a navigator's axes are **unique**: each is one level's,
  // so the editor keeps a level from taking an axis another already has.
  EXPECT_EQ(levels["axisNames"][0], "mean");
  EXPECT_EQ(levels["unique"], true);
  // The words stay the widget's own: a reshape's rows are levels.
  EXPECT_FALSE(levels.contains("groupLabel"));
  EXPECT_FALSE(levels.contains("itemLabel"));
}

/**
 * A reshape is offered a worker count, and it starts from 1.
 *
 * Whether more threads help depends on how much work one bin of the first layer holds, so the form
 * offers the choice and leaves it off until it is asked for — a reshape that is fast serially must not
 * pay for workers nobody wanted.
 */
TEST_F(ToolNgnt, TheReshapeOffersOneWorkerByDefault)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  const auto call = fTool->Call("ndmspc/ngnt/open", "POST", json{{"file", kNgntFile}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success") << call.reply.dump();

  const json & workers = call.ws["workspace"]["reshape"]["properties"]["nWorkers"];
  ASSERT_TRUE(workers.contains("type")) << workers.dump();
  EXPECT_EQ(workers["type"], "integer");
  // The form opens on every CPU the process may use: a reshape reads each entry, so serial is the exception.
  EXPECT_EQ(workers["default"], std::max(1, static_cast<int>(Ndmspc::NUtils::AvailableCpuCount())))
      << "the worker count must default to the available CPUs, not to 1";
  EXPECT_GT(workers["default"].get<int>(), 0);
  EXPECT_FALSE(workers.value("description", std::string()).empty());
}

/** The worker count the form was left at is what the workspace offers back next time. */
TEST_F(ToolNgnt, TheReshapeRemembersTheWorkerCount)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  const auto opened = fTool->Call("ndmspc/ngnt/open", "POST", json{{"file", kNgntFile}});
  ASSERT_EQ(opened.reply.value("result", std::string()), "success") << opened.reply.dump();

  const auto call = fTool->Call("ndmspc/ngnt/reshape", "POST",
                                json{{"binningName", "default"},
                                     {"levels", json::array({json::array({0}), json::array({1, 2})})},
                                     {"nWorkers", 4}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success") << call.reply.dump();

  const json & workers = call.ws["workspace"]["reshape"]["properties"]["nWorkers"];
  ASSERT_TRUE(workers.contains("default")) << workers.dump();
  EXPECT_EQ(workers["default"], 4);
}

/**
 * A second reshape of the same combination is served from the cache.
 *
 * Building a navigator reads the tree entry by entry, and the same file/binning/levels always give the
 * same one — so the second request loads it and says so, which is also why its progress bars do not
 * appear. `refresh` asks for the reshape anyway.
 *
 * The cache is turned on here rather than assumed: the suite may run with NDMSPC_RESHAPE_CACHE=0 to
 * prove the cache is optional, and a test that needed it switched on would fail for that reason.
 */
TEST_F(ToolNgnt, ARepeatedReshapeComesFromTheCache)
{
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");
  ScopedEnvVar cacheDir("NDMSPC_CACHE_DIR", "/tmp/test_toolNgnt_cache");

  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  const auto opened = fTool->Call("ndmspc/ngnt/open", "POST", json{{"file", kNgntFile}});
  ASSERT_EQ(opened.reply.value("result", std::string()), "success") << opened.reply.dump();

  const json body = {{"binningName", "default"},
                     {"levels", json::array({json::array({0}), json::array({1, 2})})}};
  const json forced = {{"binningName", "default"},
                       {"levels", json::array({json::array({0}), json::array({1, 2})})},
                       {"refresh", true}};

  // Build it from scratch first, so the entry exists whatever ran before this test.
  const auto first = fTool->Call("ndmspc/ngnt/reshape", "POST", forced);
  ASSERT_EQ(first.reply.value("result", std::string()), "success") << first.reply.dump();

  const auto second = fTool->Call("ndmspc/ngnt/reshape", "POST", body);
  ASSERT_EQ(second.reply.value("result", std::string()), "success") << second.reply.dump();
  EXPECT_TRUE(second.reply.value("cached", false)) << "a repeated reshape should be served from the cache";

  // ... and what came back is a working navigator, not merely a reply.
  const auto mapped = fTool->Call("ndmspc/ngnt/map", "POST", json::object());
  EXPECT_EQ(mapped.reply.value("result", std::string()), "success") << mapped.reply.dump();

  // A forced reshape rebuilds, and says nothing about the cache.
  const auto rebuilt = fTool->Call("ndmspc/ngnt/reshape", "POST", forced);
  ASSERT_EQ(rebuilt.reply.value("result", std::string()), "success") << rebuilt.reply.dump();
  EXPECT_FALSE(rebuilt.reply.value("cached", false)) << "refresh must reshape rather than load";
}

/**
 * The three cache modes, and where the entry goes.
 *
 * `use` loads, `rebuild` always builds, `ignore` builds and keeps nothing. Where an entry is kept is a separate
 * choice, and the default follows the input: a local file keeps its own entries, a remote one does not get
 * written to.
 */
TEST_F(ToolNgnt, CacheModesAndHomesAreIndependent)
{
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");
  ScopedEnvVar cacheDir("NDMSPC_CACHE_DIR", "/tmp/test_toolNgnt_cache");

  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  const auto opened = fTool->Call("ndmspc/ngnt/open", "POST", json{{"file", kNgntFile}});
  ASSERT_EQ(opened.reply.value("result", std::string()), "success") << opened.reply.dump();

  const json levels = json::array({json::array({0}), json::array({1, 2})});
  auto       ask    = [&](const json & extra) {
    json body = {{"binningName", "default"}, {"levels", levels}};
    for (auto it = extra.begin(); it != extra.end(); ++it) body[it.key()] = it.value();
    return fTool->Call("ndmspc/ngnt/reshape", "POST", body);
  };

  // Ignore: neither reads nor writes, so it reports neither a hit nor a store.
  const auto ignored = ask({{"cache", "ignore"}});
  ASSERT_EQ(ignored.reply.value("result", std::string()), "success") << ignored.reply.dump();
  EXPECT_FALSE(ignored.reply.value("cached", false)) << "ignore never reads";
  EXPECT_FALSE(ignored.reply.value("stored", false)) << "ignore must store nothing";

  // Rebuild: builds and stores, so it reports a store and no hit.
  const auto rebuilt = ask({{"cache", "rebuild"}});
  ASSERT_EQ(rebuilt.reply.value("result", std::string()), "success") << rebuilt.reply.dump();
  EXPECT_FALSE(rebuilt.reply.value("cached", false)) << "rebuild always reshapes";
  EXPECT_TRUE(rebuilt.reply.value("stored", false)) << "rebuild stores over what was there";
  EXPECT_EQ(rebuilt.reply.value("home", std::string()), "in-file")
      << "a local file keeps its entries unless told otherwise";

  // Use: the entry just stored is loaded, and use never writes.
  const auto used = ask({{"cache", "use"}});
  ASSERT_EQ(used.reply.value("result", std::string()), "success") << used.reply.dump();
  EXPECT_TRUE(used.reply.value("cached", false)) << "use loads what rebuild stored";
  EXPECT_FALSE(used.reply.value("stored", false));
  EXPECT_EQ(used.reply.value("home", std::string()), "in-file");

  // UseOrStore, and the same request with no mode at all: both load, because that is the default.
  const auto usedOrStored = ask({{"cache", "use-or-store"}});
  ASSERT_EQ(usedOrStored.reply.value("result", std::string()), "success") << usedOrStored.reply.dump();
  EXPECT_TRUE(usedOrStored.reply.value("cached", false)) << "use-or-store loads what is stored";

  const auto omitted = ask(json::object());
  ASSERT_EQ(omitted.reply.value("result", std::string()), "success") << omitted.reply.dump();
  EXPECT_TRUE(omitted.reply.value("cached", false)) << "the default is use-or-store";

  // An explicit empty home is the cache directory, and is not the same request as leaving it out.
  const auto elsewhere = ask({{"cache", "rebuild"}, {"cacheFile", ""}});
  ASSERT_EQ(elsewhere.reply.value("result", std::string()), "success") << elsewhere.reply.dump();
  EXPECT_EQ(elsewhere.reply.value("home", std::string()), "cache") << "'cacheFile': '' means the cache directory";

  // An unknown mode is refused rather than silently treated as the default.
  const auto bad = ask({{"cache", "sometimes"}});
  EXPECT_NE(bad.reply.value("result", std::string()), "success");
}

/**
 * The published schema offers the four modes and the two homes, and its defaults follow the input.
 *
 * A local file opens on *Input file*; a remote one on *Cache directory*. Checked on the schema rather than by
 * opening a remote file, so the test needs no EOS.
 */
TEST_F(ToolNgnt, TheFormOffersCacheModesAndHomes)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  const auto opened = fTool->Call("ndmspc/ngnt/open", "POST", json{{"file", kNgntFile}});
  ASSERT_EQ(opened.reply.value("result", std::string()), "success") << opened.reply.dump();

  const auto reshaped = fTool->Call("ndmspc/ngnt/reshape", "POST",
                                    json{{"binningName", "default"},
                                         {"levels", json::array({json::array({0}), json::array({1, 2})})}});
  ASSERT_EQ(reshaped.reply.value("result", std::string()), "success") << reshaped.reply.dump();

  const json workspace = reshaped.ws.contains("workspace") ? reshaped.ws["workspace"] : json::object();
  ASSERT_TRUE(workspace.contains("reshape")) << workspace.dump();
  const json props = workspace["reshape"]["properties"];

  ASSERT_TRUE(props.contains("cache")) << props.dump();
  EXPECT_EQ(props["cache"]["default"], "use-or-store") << "the default fills the cache as it is used";
  EXPECT_EQ(props["cache"]["enum"], json::array({"use", "use-or-store", "rebuild", "ignore"}));
  EXPECT_EQ(props["cache"]["format"], "segmented") << "four answers read best as a row of buttons";
  ASSERT_TRUE(props.contains("cacheFile")) << props.dump();
  EXPECT_EQ(props["cacheFile"]["format"], "select");
  EXPECT_EQ(props["cacheFile"]["default"], std::string(kNgntFile))
      << "a local input opens on storing in itself";

  // The choices are the two homes as **paths**, so the dropdown reads as where an entry lands rather than as
  // a pair of categories — and neither is the empty string, which a select draws as "nothing chosen".
  const json homes = props["cacheFile"]["enum"];
  ASSERT_EQ(homes.size(), 2u) << homes.dump();
  EXPECT_NE(homes[0], "") << "the cache file is named by its path";
  EXPECT_EQ(homes[1], std::string(kNgntFile)) << "and the input by its own";
  EXPECT_EQ(props["cacheFile"]["labels"].size(), 2u) << props["cacheFile"]["labels"].dump();
  EXPECT_NE(props["cacheFile"]["labels"][0], props["cacheFile"]["labels"][1])
      << "the two choices must not read the same, or the dropdown is a coin flip";

  // What the reshape *is* comes first; the worker count belongs to the reshape rather than to where its
  // result is kept, so it stays at the root above the cache section instead of inside it.
  const json reshape = workspace["reshape"];
  ASSERT_TRUE(reshape.contains("order")) << reshape.dump();
  EXPECT_EQ(reshape["order"], json::array({"binningName", "levels", "nWorkers", "cache", "cacheFile"}))
      << "binning and levels first, then workers, then everything about the cache";
  ASSERT_TRUE(reshape.contains("groups")) << reshape.dump();
  ASSERT_EQ(reshape["groups"].size(), 1u) << reshape["groups"].dump();
  EXPECT_EQ(reshape["groups"][0]["title"], "Cache");
  EXPECT_EQ(reshape["groups"][0]["fields"], json::array({"cache", "cacheFile"}))
      << "workers is not a cache field";
  EXPECT_EQ(reshape["groups"][0]["collapsed"], true)
      << "the cache section starts closed: the reshape is about the binning and the levels";

  // The remote case, on the schema alone: a root:// name defaults to the cache file, never to a path on the
  // server that a default must not write to.
  const auto remote = fTool->Call("ndmspc/ngnt/open", "POST",
                                  json{{"file", "root://eos.example.com//eos/a/data.root"}});
  if (remote.reply.value("result", std::string()) == "success") {
    const json remoteWs = remote.ws.contains("workspace") ? remote.ws["workspace"] : json::object();
    ASSERT_TRUE(remoteWs.contains("reshape")) << remoteWs.dump();
    const json remoteProps = remoteWs["reshape"]["properties"];
    EXPECT_NE(remoteProps["cacheFile"]["default"], "root://eos.example.com//eos/a/data.root")
        << "a remote input must not default to writing the input";
    EXPECT_EQ(remoteProps["cacheFile"]["default"], remoteProps["cacheFile"]["enum"][0])
        << "it opens on the cache file, which is the first choice offered";
  }
}

/// Open the tutorial's tree and reshape it the way the tests below draw it: two layers, `[mean]` and `[sigma]`.
void OpenAndReshape(ToolHarness & tool)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  ASSERT_EQ(tool.Call("ndmspc/ngnt/open", "POST", {{"file", kNgntFile}}).reply["result"], "success");
  ASSERT_EQ(tool.Call("ndmspc/ngnt/reshape", "POST",
                      {{"binningName", "default"},
                       {"levels", json::array({json::array({0}), json::array({1, 2})})}})
                .reply["result"],
            "success");
}

/// Whether two JSON arrays of numbers agree.
///
/// ROOT's serializer writes a zero as `0` where the export has `0.0` (and JSON equality is by type), and a
/// value that is copied into a histogram and serialized again is not bit-identical to the one it came from:
/// the arrays are the same numbers, so they are compared as such.
bool CloseNumbers(const json & left, const json & right, double tolerance = 1e-12)
{
  if (!left.is_array() || !right.is_array() || left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); i++) {
    if (!left[i].is_number() || !right[i].is_number()) return false;
    const double a = left[i].get<double>();
    const double b = right[i].get<double>();
    if (std::fabs(a - b) > tolerance * std::max(1.0, std::fabs(a))) return false;
  }
  return true;
}

/**
 * Every layer draws the values of **its own** slice of the export.
 *
 * A deeper layer is a child node of the export, not the root again: the root's own `fArray` is the 0/1 presence
 * mask (the export overwrites it), so the first set cell names the child bin the walk descended into.
 */
TEST_F(ToolNgnt, EveryLayerShowsItsOwnSlicesValues)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall byMean = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(byMean.reply["result"], "success") << byMean.reply.dump();
  const json pads = ToolHarness::Pads(byMean);
  ASSERT_GE(pads.size(), 2u) << "the mapping draws the navigator's two levels";

  const json & root = pads[0]["value"]["nested"];
  ASSERT_TRUE(root.contains("fArray")) << root.dump();
  ASSERT_TRUE(root.contains("children")) << root.dump();

  // The export's root `fArray` is the presence mask: the first set cell is the child the layer walk took.
  int childBin = -1;
  for (size_t i = 0; i < root["fArray"].size() && childBin < 0; i++) {
    if (root["fArray"][i].is_number() && root["fArray"][i].get<double>() != 0.0) childBin = static_cast<int>(i);
  }
  ASSERT_GE(childBin, 0) << root["fArray"].dump();

  const json & child = root["children"]["content"][childBin];
  ASSERT_TRUE(child.contains("fArrays")) << child.dump();
  ASSERT_TRUE(child["fArrays"].contains("meanFit")) << child["fArrays"].dump();

  // The deeper layer's cells are that child's values, not the root's.
  EXPECT_TRUE(CloseNumbers(pads[1]["value"]["fArray"], child["fArrays"]["meanFit"]["values"]))
      << "the second layer must show its own slice";
  EXPECT_FALSE(CloseNumbers(pads[0]["value"]["fArray"], pads[1]["value"]["fArray"]))
      << "the two layers are different slices of the tree";
}

/**
 * The map's form offers what a mapping can show: the entry ids its bins hold, or one of the tree's
 * parameters — the same numbers the export carries for the 3D view — plus the range and error switches.
 */
TEST_F(ToolNgnt, TheMapFormOffersParametersRangeAndErrors)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNgntFile}}).reply["result"], "success");
  // The map's schema is published by the reshape, which is where this form is first offered.
  const Ndmspc::Test::ToolCall reshaped =
      fTool->Call("ndmspc/ngnt/reshape", "POST",
                  {{"binningName", "default"},
                   {"levels", json::array({json::array({0}), json::array({1, 2})})}});
  ASSERT_EQ(reshaped.reply["result"], "success") << reshaped.reply.dump();
  const json workspace = reshaped.ws.contains("workspace") ? reshaped.ws["workspace"] : json::object();
  ASSERT_TRUE(workspace.contains("map")) << workspace.dump();
  const json & schema = workspace["map"];
  ASSERT_TRUE(schema.contains("properties")) << schema.dump();
  const json & props = schema["properties"];

  ASSERT_TRUE(props.contains("content")) << props.dump();
  EXPECT_EQ(props["content"]["format"], "select");
  EXPECT_EQ(props["content"]["default"], "id") << "a mapping shows the entry ids until asked otherwise";
  EXPECT_EQ(props["content"]["refresh"], true) << "changing it re-draws the mapping without a run";
  // The ids first, then the parameters this tree carries.
  EXPECT_EQ(props["content"]["enum"], json::array({"id", "meanFit", "sigmaFit"})) << props["content"].dump();

  ASSERT_TRUE(props.contains("fixedRange")) << props.dump();
  EXPECT_EQ(props["fixedRange"]["type"], "boolean");
  EXPECT_EQ(props["fixedRange"]["default"], false) << "the canvas fits what a slice draws until asked otherwise";
  ASSERT_TRUE(props.contains("errors")) << props.dump();
  EXPECT_EQ(props["errors"]["type"], "boolean");
  EXPECT_EQ(props["errors"]["default"], false) << "errors are not shown until they are asked for";

  EXPECT_EQ(schema["order"], json::array({"content", "errors", "fixedRange", "mappingPad", "contentPad", "averages"}))
      << schema.dump();
}

/**
 * A mapping can show one of the tree's parameters instead of the entry ids its bins hold.
 *
 * The values are the ones the nested export carries for the same cells (`fArrays`), which is what the 3D
 * view reads: the tab keeps the layer's label, so it is the same tab with different cells, and the ids are
 * still one request away.
 */
TEST_F(ToolNgnt, AMapShowsAParameterInTheBins)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall byId = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(byId.reply["result"], "success") << byId.reply.dump();
  const Ndmspc::Test::ToolCall byMean = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(byMean.reply["result"], "success") << byMean.reply.dump();

  const json idPads   = ToolHarness::Pads(byId);
  const json meanPads = ToolHarness::Pads(byMean);
  ASSERT_FALSE(idPads.empty()) << byId.ws.dump();
  ASSERT_FALSE(meanPads.empty()) << byMean.ws.dump();

  const json & value = meanPads[0]["value"];
  // The cells hold the parameter, read from the same export this layer carries for the 3D view.
  ASSERT_TRUE(value.contains("nested")) << value.dump();
  ASSERT_TRUE(value["nested"].contains("fArrays")) << value["nested"].dump();
  const json & arrays = value["nested"]["fArrays"]["meanFit"];
  ASSERT_TRUE(arrays.contains("values")) << arrays.dump();
  EXPECT_TRUE(CloseNumbers(value["fArray"], arrays["values"]));
  // The layer also says which quantity it holds, so a viewer (the 3D scene) can open on it instead of the ids.
  EXPECT_EQ(value["content"], "meanFit") << value.dump();
  EXPECT_EQ(idPads[0]["value"]["content"], "id") << idPads[0]["value"].dump();

  // Still the layer's own tab, over the same cells: what changed is what they hold.
  EXPECT_EQ(meanPads[0]["label"], idPads[0]["label"]);
  EXPECT_EQ(value["fArray"].size(), idPads[0]["value"]["fArray"].size());
  EXPECT_FALSE(CloseNumbers(value["fArray"], idPads[0]["value"]["fArray"]))
      << "the ids and the parameter are not the same numbers";
}

/**
 * The canvas fits what a slice draws, and a layer of **labelled** bins keeps that fit even when asked otherwise.
 *
 * Left off (the default), ROOT's unset sentinel travels with the histogram and jsroot fits the cells it draws.
 * Asked for, a range is only for a continuous quantity: this fixture's sparse binning **names every bin**
 * (`mean = -2.5`, `entries = 1000`), so its layers are all categorical and none takes the export's range. A
 * layer of measured, unlabelled bins would take it — no fixture here has one.
 */
TEST_F(ToolNgnt, AMapLetsTheCanvasFitAndKeepsItForLabelledLayers)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall fitting = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(fitting.reply["result"], "success") << fitting.reply.dump();
  const json fittingValue = ToolHarness::Pads(fitting)[0]["value"];
  EXPECT_DOUBLE_EQ(fittingValue["fMinimum"].get<double>(), -1111.0) << "ROOT's unset range: the canvas fits";
  EXPECT_DOUBLE_EQ(fittingValue["fMaximum"].get<double>(), -1111.0);

  const Ndmspc::Test::ToolCall forced =
      fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}, {"fixedRange", true}});
  ASSERT_EQ(forced.reply["result"], "success") << forced.reply.dump();
  const json forcedValue = ToolHarness::Pads(forced)[0]["value"];
  EXPECT_DOUBLE_EQ(forcedValue["fMinimum"].get<double>(), -1111.0)
      << "a layer of labelled bins is not given a numeric range";
  EXPECT_DOUBLE_EQ(forcedValue["fMaximum"].get<double>(), -1111.0);
}

/**
 * The parameter's errors are the second reading of the same layer: the bins hold them instead of the values.
 *
 * `fArrays`' errors are written as the **square** of the parameter error (`ExportToJson`), and the 3D view
 * scales by them as they are stored (`THnPainter::getBinError`), so they go into the cells unchanged — no
 * square root on the way in, and no error bars either, because the errors *are* the picture. Off by default:
 * a mapping is read for its values.
 */
TEST_F(ToolNgnt, AMapDrawsAParametersErrorsWhenAsked)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall plain = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(plain.reply["result"], "success") << plain.reply.dump();
  const json plainValue = ToolHarness::Pads(plain)[0]["value"];
  EXPECT_TRUE(plainValue["fSumw2"].empty()) << "a mapping of values carries no errors";
  EXPECT_FALSE(plainValue.contains("errors")) << plainValue.dump();
  EXPECT_EQ(plainValue["fTitle"], "meanFit") << plainValue.dump();

  const Ndmspc::Test::ToolCall withErrors =
      fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}, {"errors", true}});
  ASSERT_EQ(withErrors.reply["result"], "success") << withErrors.reply.dump();
  const json envelope = ToolHarness::Pads(withErrors)[0];
  const json value    = envelope["value"];
  const json arrays   = value["nested"]["fArrays"]["meanFit"];
  const json errors   = arrays["errors"];
  ASSERT_FALSE(errors.empty()) << errors.dump();
  EXPECT_TRUE(CloseNumbers(value["fArray"], errors)) << "the cells hold the export's errors, as stored";
  EXPECT_FALSE(CloseNumbers(value["fArray"], arrays["values"])) << "and not the values they belong to";
  EXPECT_TRUE(value["fSumw2"].empty()) << "the errors are the picture, not bars drawn on it";
  EXPECT_EQ(value["errors"], true) << "and the layer says so, so a viewer can scale by the error";
  EXPECT_EQ(value["fName"], "mean_meanFit_errors") << value.dump();
  EXPECT_EQ(value["fTitle"], "meanFit errors") << value.dump();
}

/**
 * A layer with no values to show falls back to its own projection rather than going blank.
 *
 * With averaging off, a deeper level of the export carries only min/max — there is no per-cell value to put
 * in the bins — so that layer draws the entry ids it always did.
 */
TEST_F(ToolNgnt, AParameterWithoutValuesFallsBackToTheProjection)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall byId = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(byId.reply["result"], "success") << byId.reply.dump();

  const Ndmspc::Test::ToolCall plain =
      fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}, {"averages", false}});
  ASSERT_EQ(plain.reply["result"], "success") << plain.reply.dump();
  const json pads = ToolHarness::Pads(plain);
  ASSERT_FALSE(pads.empty()) << plain.ws.dump();
  const json & value = pads[0]["value"];
  // The root has no values of its own when nothing was averaged, so its bins are the ids again.
  EXPECT_FALSE(value["nested"]["fArrays"]["meanFit"].contains("values"))
      << value["nested"]["fArrays"]["meanFit"].dump();
  EXPECT_TRUE(CloseNumbers(value["fArray"], ToolHarness::Pads(byId)[0]["value"]["fArray"]));
}

/**
 * Changing what a mapping shows re-draws it in place: the layer's own tab is replaced and no step is added.
 *
 * That is the request a form's `refresh` field makes — a PATCH of the step itself, with no drill
 * coordinate — and it is what keeps the user on the tab they were looking at.
 */
TEST_F(ToolNgnt, ARedrawPatchReplacesTheLayersWithoutAddingAStep)
{
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");
  ScopedEnvVar cacheDir("NDMSPC_CACHE_DIR", "/tmp/test_toolNgnt_cache");

  Ndmspc::NHttpServer * server = fTool->Server();

  const json opened = Dispatch(server, "POST", "ndmspc/ngnt/open", {{"file", kNgntFile}});
  ASSERT_EQ(opened["result"], "success") << opened.dump();
  const std::string openId = opened["combination"]["id"];

  const json reshaped = Dispatch(server, "POST", "ndmspc/ngnt/reshape",
                                 {{"binningName", "default"},
                                  {"levels", json::array({json::array({0}), json::array({1, 2})})},
                                  {"refresh", true},
                                  {"path", json::array({openId})}});
  ASSERT_EQ(reshaped["result"], "success") << reshaped.dump();
  const std::string reshapeId = reshaped["combination"]["id"];

  const json mapped =
      Dispatch(server, "POST", "ndmspc/ngnt/map", {{"path", json::array({openId, reshapeId})}});
  ASSERT_EQ(mapped["result"], "success") << mapped.dump();
  const std::string mapId = mapped["combination"]["id"];

  const json redrawn = Dispatch(server, "PATCH", "ndmspc/ngnt/map",
                                {{"content", "sigmaFit"}, {"path", json::array({openId, reshapeId, mapId})}});
  ASSERT_EQ(redrawn["result"], "success") << redrawn.dump();
  EXPECT_FALSE(redrawn.contains("combination")) << "a redraw must not create a step: " << redrawn.dump();

  // The step the request named now says what it shows, and it is the same node.
  Ndmspc::NInstanceTree tree(server->GetCombinations());
  ASSERT_TRUE(tree.Has(mapId)) << tree.ToTree().dump();
  EXPECT_EQ(tree.Params(mapId).value("content", std::string()), "sigmaFit") << tree.Params(mapId).dump();
  ASSERT_TRUE(tree.Params(mapId).contains("fixedRange")) << tree.Params(mapId).dump();
  EXPECT_EQ(tree.Params(mapId)["fixedRange"], false) << "the form did not ask for the export's range";
}

/**
 * The redraw's envelopes replace the layer's own tab where it stands.
 *
 * `replace` is what a *fresh* map asks for, so its pad drops what it held; a redraw passes it off, which is
 * what keeps the user on the tab they were looking at — and no `reveal` is asked for either. No export is
 * assumed to be kept: a redraw is a deliberate change, so it may produce the one it needs.
 */
TEST_F(ToolNgnt, ARedrawReplacesTheTabsItAlreadyDrew)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall first = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(first.reply["result"], "success") << first.reply.dump();
  const json firstPads = ToolHarness::Pads(first);
  ASSERT_FALSE(firstPads.empty()) << first.ws.dump();
  EXPECT_TRUE(firstPads[0].contains("replace")) << "a fresh map owns the pad";

  const Ndmspc::Test::ToolCall redrawn = fTool->Call("ndmspc/ngnt/map", "PATCH", {{"content", "sigmaFit"}});
  ASSERT_EQ(redrawn.reply["result"], "success") << redrawn.reply.dump();
  const json pads = ToolHarness::Pads(redrawn);
  ASSERT_FALSE(pads.empty()) << redrawn.ws.dump();
  EXPECT_FALSE(pads[0].contains("replace")) << "a redraw replaces the tab it belongs to: " << pads[0].dump();
  EXPECT_FALSE(redrawn.ws["payload"].contains("reveal")) << "and it does not move the tab the user is on";
  EXPECT_EQ(pads[0]["label"], firstPads[0]["label"]) << "the layer keeps its tab";
  // The redraw names the tree it belongs to and leaves the export out — it is big, and a viewer keeps the copy the
  // drawing that owned the pad sent. The cells are the newly chosen parameter, read from that same export.
  EXPECT_EQ(pads[0]["value"]["generation"], firstPads[0]["value"]["generation"]) << pads[0]["value"].dump();
  EXPECT_FALSE(pads[0]["value"].contains("nested"))
      << "a redraw does not repeat the export: " << pads[0]["value"].dump();
  const json & arrays = firstPads[0]["value"]["nested"]["fArrays"]["sigmaFit"];
  EXPECT_TRUE(CloseNumbers(pads[0]["value"]["fArray"], arrays["values"]))
      << "the layer is re-drawn with the newly chosen parameter";
}

/**
 * A leaf marks where its level ends: `children`, empty.
 *
 * A viewer reads a tree by its keys rather than by its shape, so even a one-level export is recognised as a nested
 * drawing. Its own file, because the export is kept beside the data and keyed by data and levels — not by the code
 * that wrote it — so the shared one could answer with an export from an older build.
 */
TEST_F(ToolNgnt, ALeafSaysItsLevelEnds)
{
  const std::string file = "/tmp/test_toolNgnt_leaf.root";
  std::remove(file.c_str());
  NSingleBinning01Gaus(file);

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", file}}).reply["result"], "success");
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"},
                         {"levels", json::array({json::array({0}), json::array({1, 2})})}})
                .reply["result"],
            "success");
  const Ndmspc::Test::ToolCall mapped = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(mapped.reply["result"], "success") << mapped.reply.dump();
  const json pads = ToolHarness::Pads(mapped);
  ASSERT_FALSE(pads.empty()) << mapped.ws.dump();
  const json & root = pads[0]["value"]["nested"];
  ASSERT_TRUE(root.contains("children")) << pads[0]["value"].dump();
  const json & kids = root["children"]["content"];
  ASSERT_TRUE(kids.is_array()) << root.dump();

  int childBin = -1;
  for (size_t i = 1; i < kids.size() && childBin < 0; i++) {
    if (kids[i].is_object()) childBin = static_cast<int>(i);
  }
  ASSERT_GE(childBin, 0) << kids.dump();
  const json & child = kids[childBin];
  ASSERT_TRUE(child.contains("children")) << child.dump();
  const json & below = child["children"];
  ASSERT_TRUE(below.contains("content")) << below.dump();
  EXPECT_TRUE(below["content"].is_array()) << below.dump();
  EXPECT_TRUE(below["content"].empty()) << "a leaf has nothing below it: " << below.dump();
}

/**
 * A redraw of a navigator this step has never drawn replaces the pad.
 *
 * A redraw keeps the pad on purpose — that is what keeps the user on the tab they are looking at — but the first
 * map after a **reshape** draws a new navigator, and the previous reshape's tabs would stay beside the new drawing
 * (a pad merges tabs by label). A viewer that reads the pad's tabs for its scene could then keep showing the old
 * tree, so the reshape stamps a generation, the map records the one it drew, and a redraw that finds them different
 * replaces instead of appending.
 */
TEST_F(ToolNgnt, ARedrawAfterANewReshapeReplacesThePad)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall first = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(first.reply["result"], "success") << first.reply.dump();
  EXPECT_TRUE(ToolHarness::Pads(first)[0].contains("replace")) << "a fresh map owns the pad";

  const Ndmspc::Test::ToolCall sameNavigator = fTool->Call("ndmspc/ngnt/map", "PATCH", {{"content", "sigmaFit"}});
  ASSERT_EQ(sameNavigator.reply["result"], "success") << sameNavigator.reply.dump();
  EXPECT_FALSE(ToolHarness::Pads(sameNavigator)[0].contains("replace"))
      << "the navigator did not change, so the redraw keeps the pad";

  // A second reshape builds a new navigator.
  const Ndmspc::Test::ToolCall reshaped =
      fTool->Call("ndmspc/ngnt/reshape", "POST",
                  {{"binningName", "default"},
                   {"levels", json::array({json::array({0}), json::array({1, 2})})}});
  ASSERT_EQ(reshaped.reply["result"], "success") << reshaped.reply.dump();

  const Ndmspc::Test::ToolCall afterReshape = fTool->Call("ndmspc/ngnt/map", "PATCH", {{"content", "meanFit"}});
  ASSERT_EQ(afterReshape.reply["result"], "success") << afterReshape.reply.dump();
  const json pads = ToolHarness::Pads(afterReshape);
  ASSERT_FALSE(pads.empty()) << afterReshape.ws.dump();
  EXPECT_TRUE(pads[0].contains("replace"))
      << "a new navigator replaces what the pad held, so no tab of the old one survives: " << pads[0].dump();

  const Ndmspc::Test::ToolCall again = fTool->Call("ndmspc/ngnt/map", "PATCH", {{"content", "sigmaFit"}});
  ASSERT_EQ(again.reply["result"], "success") << again.reply.dump();
  EXPECT_FALSE(ToolHarness::Pads(again)[0].contains("replace"))
      << "the generation is recorded by the draw that consumed it";
}

/**
 * A redraw of the same navigator says so, so a viewer can keep the scene it already has.
 *
 * The map form's own fields (`content`, `errors`, `fixedRange`) redraw the same tree, and so does a drill: the
 * drawing carries the navigator's `generation`, which a new reshape bumps and nothing else changes. A viewer that
 * compared it would rebuild its scene only for a new navigator — rebuilding costs the level the reader walked down
 * to, since the painter starts again at the root.
 */
TEST_F(ToolNgnt, ARedrawCarriesTheGenerationOfTheTreeItDraws)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  const Ndmspc::Test::ToolCall mapped = fTool->Call("ndmspc/ngnt/map", "POST");
  ASSERT_EQ(mapped.reply["result"], "success") << mapped.reply.dump();
  const json first = ToolHarness::Pads(mapped);
  ASSERT_FALSE(first.empty()) << mapped.ws.dump();
  ASSERT_TRUE(first[0]["value"].contains("generation")) << first[0]["value"].dump();
  const json firstGeneration = first[0]["value"]["generation"];

  const Ndmspc::Test::ToolCall redrawn = fTool->Call("ndmspc/ngnt/map", "PATCH",
                                                    {{"content", "meanFit"}, {"errors", true}, {"fixedRange", true}});
  ASSERT_EQ(redrawn.reply["result"], "success") << redrawn.reply.dump();
  const json redrawnPads = ToolHarness::Pads(redrawn);
  ASSERT_FALSE(redrawnPads.empty()) << redrawn.ws.dump();
  EXPECT_EQ(redrawnPads[0]["value"]["generation"], firstGeneration)
      << "the same tree, so a viewer keeps its scene: " << redrawnPads[0]["value"].dump();
  EXPECT_EQ(redrawnPads[0]["value"]["content"], "meanFit") << "the reading it shows still travels";

  // Switching back to the entry ids only changes the 2D cells: the tree stays on the pad, because that is what a
  // 3D viewer draws (losing it would read as a different kind of drawing and rebuild the scene).
  const Ndmspc::Test::ToolCall byId = fTool->Call("ndmspc/ngnt/map", "PATCH", {{"content", "id"}});
  ASSERT_EQ(byId.reply["result"], "success") << byId.reply.dump();
  const json byIdPads = ToolHarness::Pads(byId);
  ASSERT_FALSE(byIdPads.empty()) << byId.ws.dump();
  EXPECT_EQ(byIdPads[0]["value"]["content"], "id") << byIdPads[0]["value"].dump();
  EXPECT_EQ(byIdPads[0]["value"]["generation"], firstGeneration)
      << "the same tree, so a viewer keeps its scene and its export";
  EXPECT_FALSE(byIdPads[0]["value"].contains("nested")) << "and the export is not repeated for it";

  // A new navigator is a different tree, and a viewer has to rebuild for it.
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"},
                         {"levels", json::array({json::array({0}), json::array({1, 2})})}})
                .reply["result"],
            "success");
  const Ndmspc::Test::ToolCall again = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(again.reply["result"], "success") << again.reply.dump();
  const json againPads = ToolHarness::Pads(again);
  ASSERT_FALSE(againPads.empty()) << again.ws.dump();
  EXPECT_NE(againPads[0]["value"]["generation"], firstGeneration) << "a new reshape is a new tree";
}

/**
 * A click still names its entry while the bins hold a parameter.
 *
 * The id is not in the cell any more — the parameter's value is — so it is read from the layer the click was
 * drawn on, at the bin the click names. That value may be integer-ish, so `args.cont` must not be mistaken
 * for an id.
 */
TEST_F(ToolNgnt, AClickNamesItsEntryWhileAParameterIsShown)
{
  ASSERT_NO_FATAL_FAILURE(OpenAndReshape(*fTool));

  // The ids come from a mapping that shows them: its cells hold `entry + 1`, and the navigator's two levels
  // are the two envelopes. A parameter's own cells cannot name an entry — that is the point.
  const Ndmspc::Test::ToolCall byIdMapping = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "id"}});
  ASSERT_EQ(byIdMapping.reply["result"], "success") << byIdMapping.reply.dump();
  const json idPads = ToolHarness::Pads(byIdMapping);
  ASSERT_GE(idPads.size(), 2u) << "the mapping draws the navigator's two levels";

  // A non-empty cell of the deepest layer: a ROOT bin, and the layer's children are indexed by them.
  const json & leafCells = idPads[1]["value"]["fArray"];
  int          leafBin   = -1;
  for (size_t i = 1; i < leafCells.size() && leafBin < 0; i++) {
    if (leafCells[i].is_number() && leafCells[i].get<double>() >= 1.0) leafBin = static_cast<int>(i);
  }
  ASSERT_GE(leafBin, 0) << leafCells.dump();

  // The deepest layer's cells hold `entry + 1`, which the id-mode click would have carried as `args.cont`.
  const int entry = static_cast<int>(std::lround(leafCells[leafBin].get<double>())) - 1;
  ASSERT_GE(entry, 0) << leafCells.dump();

  // The drawing the click lands on shows the parameter, not the ids.
  const Ndmspc::Test::ToolCall shown = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(shown.reply["result"], "success") << shown.reply.dump();

  // Two clicks on the same cell: with the parameter shown only the bin names the entry, and the id-mode
  // click names it through `cont`. The drawing starts at the navigator, so the click's base point is empty.
  const Ndmspc::Test::ToolCall byParameter =
      fTool->Call("ndmspc/ngnt/map", "PATCH",
                  {{"content", "meanFit"},
                   {"point", json::array()},
                   {"level", 1},
                   {"args", {{"bin", leafBin}}}});
  ASSERT_EQ(byParameter.reply["result"], "success") << byParameter.reply.dump();
  const Ndmspc::Test::ToolCall byId = fTool->Call("ndmspc/ngnt/map", "PATCH",
                                                  {{"content", "id"},
                                                   {"point", json::array()},
                                                   {"level", 1},
                                                   {"args", {{"bin", leafBin}, {"cont", entry + 1}}}});
  ASSERT_EQ(byId.reply["result"], "success") << byId.reply.dump();

  const auto contentOf = [](const Ndmspc::Test::ToolCall & call) {
    json names = json::array();
    for (const auto & envelope : ToolHarness::Pads(call)) {
      if (envelope.value("pad", std::string()) == "pad2") names.push_back(envelope.value("label", std::string()));
    }
    return names;
  };
  const json byParameterDrawn = contentOf(byParameter);
  const json byIdDrawn        = contentOf(byId);
  EXPECT_FALSE(byParameterDrawn.empty())
      << "a click while a parameter is shown must still find its entry: " << byParameter.ws.dump();
  EXPECT_EQ(byParameterDrawn, byIdDrawn) << "the same cell names the same entry either way";
}

/**
 * The parameters a mapping can show follow the tree that is open.
 *
 * The dropdown is built when the reshape publishes the map's schema, so another file's list has to be
 * refreshed on open — and a choice that named a parameter the new tree does not have goes back to the entry
 * ids, which is where the handler falls back to anyway.
 */
TEST_F(ToolNgnt, TheMapContentListFollowsTheOpenFile)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNgntFile}}).reply["result"], "success");
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"},
                         {"levels", json::array({json::array({0}), json::array({1, 2})})}})
                .reply["result"],
            "success");
  // A mapping run remembers the choice it was given, as the form's default.
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}}).reply["result"], "success");

  // Another file, with no parameters at all.
  WriteNoParameterNgnt(kNoParamNgntFile);
  const Ndmspc::Test::ToolCall reopened = fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNoParamNgntFile}});
  ASSERT_EQ(reopened.reply["result"], "success") << reopened.reply.dump();

  const json workspace = reopened.ws.contains("workspace") ? reopened.ws["workspace"] : json::object();
  ASSERT_TRUE(workspace.contains("map")) << workspace.dump();
  const json & content = workspace["map"]["properties"]["content"];
  EXPECT_EQ(content["enum"], json::array({"id"})) << content.dump();
  EXPECT_EQ(content["default"], "id") << "a parameter this file does not have falls back to the ids";
}

/**
 * A 3-axis layer is drawn from its own minimum.
 *
 * jsroot sizes a TH3 cell by the cube root of |content|, so a signed value would lose its sign — a `meanFit`
 * layer would grow a cube at -2.5 exactly as much as at +2.5, where the 3D view's heights ramp with the value
 * (ndmvr scales by `(value - min)/(max - min)`). The drawn cells are therefore `value - min` on a small floor:
 * the sizes grow with the value and the colours span the same interval as before. Cells the export marks 0
 * (no data) stay 0, which jsroot does not draw.
 */
TEST_F(ToolNgnt, AThreeAxisLayerIsDrawnFromItsMinimum)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  ASSERT_EQ(fTool->Call("ndmspc/ngnt/open", "POST", {{"file", kNgntFile}}).reply["result"], "success");
  ASSERT_EQ(fTool->Call("ndmspc/ngnt/reshape", "POST",
                        {{"binningName", "default"}, {"levels", json::array({json::array({0, 1, 2})})}})
                .reply["result"],
            "success");

  const Ndmspc::Test::ToolCall mapped = fTool->Call("ndmspc/ngnt/map", "POST", {{"content", "meanFit"}});
  ASSERT_EQ(mapped.reply["result"], "success") << mapped.reply.dump();
  const json pads = ToolHarness::Pads(mapped);
  ASSERT_EQ(pads.size(), 1u) << "one level, one layer: " << mapped.ws.dump();

  const json & value = pads[0]["value"];
  ASSERT_EQ(value.value("_typename", std::string()).substr(0, 3), "TH3") << value.dump();

  const json & arrays = value["nested"]["fArrays"]["meanFit"];
  ASSERT_TRUE(arrays.contains("values")) << arrays.dump();
  const json & truth = arrays["values"];
  const json & drawn = value["fArray"];
  ASSERT_EQ(drawn.size(), truth.size());

  double low   = 0.0;
  double high  = 0.0;
  bool   known = false;
  for (const auto & one : truth) {
    if (!one.is_number() || one.get<double>() == 0.0) continue;
    low   = known ? std::min(low, one.get<double>()) : one.get<double>();
    high  = known ? std::max(high, one.get<double>()) : one.get<double>();
    known = true;
  }
  ASSERT_TRUE(known) << truth.dump();
  const double floor = 1e-3 * (high - low);

  for (size_t i = 0; i < truth.size(); i++) {
    const double original = truth[i].get<double>();
    if (original == 0.0) {
      EXPECT_DOUBLE_EQ(drawn[i].get<double>(), 0.0) << "an empty cell must stay empty at " << i;
      continue;
    }
    EXPECT_GT(drawn[i].get<double>(), 0.0) << "a cell with data must be drawn above zero at " << i;
    EXPECT_NEAR(drawn[i].get<double>(), (original - low) + floor, 1e-9 * std::max(1.0, std::fabs(original)))
        << "the cell must be its offset from the layer's minimum, at " << i;
  }
}

/**
 * A step says what it ran with, even when the request left the arguments out.
 *
 * The inspector fills an existing step's form from the node's parameters, so a request that omits a field -
 * the pads of a map, the cache a reshape used, the parameter a spectra drew for - would leave the step
 * reading empty even though the handler resolved it. Recording the resolved call on the node is what makes an
 * MCP run read back the way a run from the form does.
 */
TEST_F(ToolNgnt, AStepRecordsTheArgumentsItResolved)
{
  std::ifstream existing(kNgntFile);
  if (!existing.good()) NSingleBinning01Gaus(std::string(kNgntFile));
  existing.close();

  Ndmspc::NHttpServer * server = fTool->Server();

  const json opened = Dispatch(server, "POST", "ndmspc/ngnt/open", {{"file", kNgntFile}});
  ASSERT_EQ(opened["result"], "success") << opened.dump();

  // Binning and levels are named; the worker count, the cache mode and the home are left to the handler.
  const json reshaped = Dispatch(server, "POST", "ndmspc/ngnt/reshape",
                                 {{"binningName", "default"}, {"levels", json::array({json::array({0, 1, 2})})}});
  ASSERT_EQ(reshaped["result"], "success") << reshaped.dump();
  const std::string reshapeId = reshaped["combination"]["id"];

  const json mapped = Dispatch(server, "POST", "ndmspc/ngnt/map", json::object());
  ASSERT_EQ(mapped["result"], "success") << mapped.dump();
  const std::string mapId = mapped["combination"]["id"];

  const json spectra = Dispatch(server, "POST", "ndmspc/ngnt/spectra", json::object());
  ASSERT_EQ(spectra["result"], "success") << spectra.dump();
  const std::string spectraId = spectra["combination"]["id"];

  Ndmspc::NInstanceTree tree(server->GetCombinations());
  ASSERT_EQ(tree.Active(), (std::vector<std::string>{opened["combination"]["id"].get<std::string>(), reshapeId, mapId,
                                                     spectraId}))
      << tree.ToTree().dump();

  const json reshape = tree.Params(reshapeId);
  EXPECT_EQ(reshape.value("binningName", std::string()), "default");
  EXPECT_EQ(reshape.value("levels", json()), json::array({json::array({0, 1, 2})}));
  EXPECT_EQ(reshape.value("nWorkers", 0), std::max(1, static_cast<int>(Ndmspc::NUtils::AvailableCpuCount())))
      << "the worker count the reshape used is part of the step";
  EXPECT_EQ(reshape.value("cache", std::string()), "use-or-store") << "the default cache mode is recorded";
  const std::string home = reshape.value("cacheFile", std::string());
  EXPECT_NE(home.find(kNgntFile), std::string::npos) << "a local input keeps its entries in itself: " << home;
  EXPECT_EQ(tree.Get(reshapeId)["label"].get<std::string>(), "default ([[0,1,2]])");

  const json map = tree.Params(mapId);
  EXPECT_EQ(map.value("mappingPad", std::string()), "pad1") << "the pads the map defaulted to are recorded";
  EXPECT_EQ(map.value("contentPad", std::string()), "pad2");
  EXPECT_EQ(map.value("averages", false), true);
  EXPECT_EQ(tree.Get(mapId)["label"].get<std::string>(), "pad1") << "the label reads the pad the map drew on";

  const json drawn = tree.Params(spectraId);
  EXPECT_EQ(drawn.value("parameters", json()), json::array({"meanFit"})) << "the parameter the form left out";
  EXPECT_EQ(drawn.value("startPad", std::string()), "pad3") << "the first pad the spectra landed on";
  EXPECT_EQ(drawn.value("parameter", std::string()), "meanFit") << "what the macro's line is for";
  EXPECT_EQ(drawn.value("axismargin", 0.0), 1.0);
  EXPECT_EQ(drawn.value("minmaxMode", std::string()), "V");
  EXPECT_EQ(tree.Get(spectraId)["label"].get<std::string>(), "meanFit")
      << "the step is named by the parameter it drew for, not by the JSON of a list";
}

} // namespace
