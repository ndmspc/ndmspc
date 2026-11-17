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
  EXPECT_EQ(reshape["groups"][0]["collapsed"], true) << "the cache section starts folded away";

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
