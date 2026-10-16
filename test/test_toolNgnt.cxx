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
    {"ndmspc/ngnt/spectra", "ndmspc/ngnt/map", "{{ parameters }}", 4},
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

} // namespace
