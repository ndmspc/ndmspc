/**
 * The macro a combination is written out as (NMacroBuilder), and the template that fills each step.
 *
 * The tool-side half - that the shipped macros declare what their steps are in code - is
 * test_toolNgnt.cxx and test_toolBrowser.cxx; the server-side half - that asking for one changes
 * nothing in the room - is test_NHttpServer.cxx.
 */

#include <gtest/gtest.h>

#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NInstanceTree.h"
#include "ndmspc/http/NMacroBuilder.h"

#include <string>
#include <utility>
#include <vector>

namespace {

/// @brief The tool metadata a step's code is read from, wired the way the CLI wires it.
///
/// A recipe is what a tool declares next to its handler (`NMcpToolInfo::macroLines`), so this is the whole
/// input the builder has beyond the combination itself.
class Recipes {
  public:
  Recipes() : fPrevious(Ndmspc::gNdmspcMcpTools) { Ndmspc::gNdmspcMcpTools = &fTools; }
  ~Recipes() { Ndmspc::gNdmspcMcpTools = fPrevious; }

  /// @brief Declare one action's step code — one entry per line — and the includes it needs.
  void Add(const std::string & action, std::vector<std::string> lines, std::vector<std::string> includes = {})
  {
    fTools[action] = Ndmspc::NMcpToolInfo{.macroLines = std::move(lines), .macroIncludes = std::move(includes)};
  }

  /// @brief Declare one action's step code, and that it writes no code at all (it is there for its state).
  void AddSilent(const std::string & action)
  {
    fTools[action] = Ndmspc::NMcpToolInfo{.macroSilent = true};
  }

  private:
  Ndmspc::NMcpToolMap   fTools;
  Ndmspc::NMcpToolMap * fPrevious;
};

} // namespace

TEST(NMacroBuilderTest, ADeclaredStepIsWrittenAsItsToolSaysItIs)
{
  Recipes recipes;
  recipes.Add("demo/open", {"auto * file = TFile::Open({{ file }});"}, {"TFile.h"});
  recipes.Add("demo/object", {"auto * object = ({{ cls:bare }} *) file->Get({{ key }});"});

  const Ndmspc::NMacroDocument doc = Ndmspc::BuildCombinationMacro(
      {{"demo/open", {{"file", "cernstaff.root"}}}, {"demo/object", {{"key", "hpx"}, {"cls", "TH1D"}}}}, "demo 1");

  // The steps and nothing else: the includes the tools asked for, then the steps' lines in order.
  EXPECT_EQ(doc.name, "demo_1");
  EXPECT_EQ(doc.text, "// demo 1: the room's active combination\n"
                      "// Written read-only - nothing in the room was changed.\n"
                      "\n"
                      "#include <TFile.h>\n"
                      "\n"
                      "void demo_1()\n"
                      "{\n"
                      "  auto * file = TFile::Open(\"cernstaff.root\");\n"
                      "  auto * object = (TH1D *) file->Get(\"hpx\");\n"
                      "}\n");
}

/// A step may be more than one line — a temporary, then the object it makes — and the lines come out in
/// the order the tool wrote them, indented into the function as one step.
TEST(NMacroBuilderTest, AStepMayBeSeveralLines)
{
  Recipes recipes;
  recipes.Add("demo/point", {"ngnt->GetEntry({{ entry }});",
                             "auto * objects = (TList *) ngnt->GetStorageTree()->GetBranchObject(\"_outputPoint\");"});

  const Ndmspc::NMacroDocument doc =
      Ndmspc::BuildCombinationMacro({{"demo/point", {{"entry", 3}}}}, "combo");

  EXPECT_NE(doc.text.find("  ngnt->GetEntry(3);\n"
                          "  auto * objects = (TList *) ngnt->GetStorageTree()->GetBranchObject(\"_outputPoint\");\n"),
            std::string::npos)
      << doc.text;
}

TEST(NMacroBuilderTest, AStepNoToolDescribesInCodeIsNamedRatherThanDropped)
{
  Recipes recipes; // nothing declared: the step is still part of the combination

  const Ndmspc::NMacroDocument doc = Ndmspc::BuildCombinationMacro({{"demo/thing", {{"a", 1}}}}, "combo");

  EXPECT_NE(doc.text.find("  // demo/thing {\"a\":1}\n"), std::string::npos) << doc.text;
  EXPECT_EQ(doc.text.find("#include"), std::string::npos);
}

/// A step whose tool declares *no* code reads as a comment naming it — never silently dropped, and never as
/// a line the tool did not write.
TEST(NMacroBuilderTest, AStepWithAClosedRecipeIsALineTheToolWrote)
{
  Recipes recipes;
  recipes.Add("demo/pick", {"auto * object = ({{ cls:bare }} *) file->Get({{ key }});"});

  // The class and the key are both the step's own arguments, so the line is the room's own reading of the
  // file — a tree comes out a `TTree *`, a histogram a `TH1D *`.
  const Ndmspc::NMacroDocument tree = Ndmspc::BuildCombinationMacro(
      {{"demo/pick", {{"key", "ntuple"}, {"cls", "TNtuple"}}}}, "one");
  EXPECT_NE(tree.text.find("  auto * object = (TNtuple *) file->Get(\"ntuple\");\n"), std::string::npos)
      << tree.text;

  // A step that recorded no class still reads as the code the tool declared: the placeholder is left out
  // and the line stays what it says it is (`TObject` is what the browser records for a pick that chose
  // nothing, which is the same shape).
  Recipes other;
  other.Add("demo/pick", {"auto * object = (TObject *) file->Get({{ key }});"});
  const Ndmspc::NMacroDocument bare =
      Ndmspc::BuildCombinationMacro({{"demo/pick", json::object()}}, "two");
  EXPECT_NE(bare.text.find("  auto * object = (TObject *) file->Get();\n"), std::string::npos) << bare.text;
}

/// A step's line may read what the step **recorded for itself** (`{{ state.<key> }}`) rather than what it was
/// given: a drill's resolved point, the object a browser step picked — neither of which a form ever sends.
TEST(NMacroBuilderTest, ALineMayReadWhatTheStepRecordedForItself)
{
  Recipes recipes;
  recipes.Add("demo/layer",
              {"auto * projection = nav->GetNavigator({{ state.point }})->GetProjection();",
               "auto * object = ({{ state.cls:bare }} *) file->Get({{ state.key }});"});

  // A step that recorded its state: the layer line carries the point, and the object is read by key and class.
  const Ndmspc::NMacroDocument drilled = Ndmspc::BuildCombinationMacro(
      {{"demo/layer",
        {{"key", "hpx"}, {"cls", "TH1D"}},
        {{"key", "ntuple"}, {"cls", "TNtuple"}, {"point", json::array({1, 2})}}}},
      "one");
  EXPECT_NE(drilled.text.find("  auto * projection = nav->GetNavigator({1, 2})->GetProjection();\n"),
            std::string::npos)
      << drilled.text;
  EXPECT_NE(drilled.text.find("  auto * object = (TNtuple *) file->Get(\"ntuple\");\n"), std::string::npos)
      << drilled.text;

  // Nothing recorded: the same line comes out with the point empty, which reaches the navigator itself — the
  // level the navigator is on, which is what a combination with no drill is.
  const Ndmspc::NMacroDocument bare =
      Ndmspc::BuildCombinationMacro({{"demo/layer", json::object(), {{"point", json::array()}}}}, "two");
  EXPECT_NE(bare.text.find("  auto * projection = nav->GetNavigator({})->GetProjection();\n"), std::string::npos)
      << bare.text;
}

/// A step may say it writes nothing at all (`macroSilent`): it is there to record something the later steps
/// read, and the export leaves no line — not even the comment that names an unfinished one.
TEST(NMacroBuilderTest, ASilentStepWritesNothing)
{
  Recipes recipes;
  recipes.AddSilent("demo/point");
  recipes.Add("demo/object", {"auto * object = ({{ state.cls:bare }} *) file->Get({{ state.key }});"});

  const Ndmspc::NMacroDocument doc = Ndmspc::BuildCombinationMacro(
      {{"demo/point", {{"entry", 3}}, {{"point", json::array({0})}}},
       {"demo/object", json::object(), {{"key", "hpx"}, {"cls", "TH1D"}}}},
      "one");

  EXPECT_EQ(doc.text.find("demo/point"), std::string::npos) << doc.text; // no line, no comment
  EXPECT_NE(doc.text.find("  auto * object = (TH1D *) file->Get(\"hpx\");\n"), std::string::npos) << doc.text;
}

TEST(NMacroBuilderTest, TheArgumentsAreWrittenAsTheTypesTheyAre)
{
  Recipes recipes;
  recipes.Add("demo/reshape", {"nav->Reshape({{ binningName }}, {{ levels }});"});

  const json levels = json::array({json::array({0, 1, 2}), json::array({3})});
  const Ndmspc::NMacroDocument doc =
      Ndmspc::BuildCombinationMacro({{"demo/reshape", {{"binningName", "default"}, {"levels", levels}}}}, "c");

  // A string is quoted and escaped by the renderer, a nested array is the initializer its argument
  // type takes - the recipe writes neither, so a value can never break the code it lands in.
  EXPECT_NE(doc.text.find("  nav->Reshape(\"default\", {{0, 1, 2}, {3}});\n"), std::string::npos) << doc.text;
}

TEST(NMacroBuilderTest, ASessionNameThatIsNotAnIdentifierBecomesOne)
{
  Recipes recipes;
  recipes.Add("demo/x", {"doIt();"});

  // ROOT names a macro after the function in it, so the file name follows the same identifier.
  EXPECT_EQ(Ndmspc::BuildCombinationMacro({{"demo/x", json::object()}}, "ngnt 1").name, "ngnt_1");
  EXPECT_EQ(Ndmspc::BuildCombinationMacro({{"demo/x", json::object()}}, "2nd run!").name, "c_2nd_run");
  EXPECT_EQ(Ndmspc::BuildCombinationMacro({{"demo/x", json::object()}}, "").name, "combination");
}

TEST(NMacroBuilderTest, TheTemplateWritesLabelsAsItAlwaysHasAndCodeAsItIsWritten)
{
  const json params = {{"binningName", "b0"}};

  // A label leaves out what the node has no argument for, and tidies the gap it leaves.
  EXPECT_EQ(Ndmspc::RenderTemplate("{{ binningName }} ({{ levels }})", params), "b0 ()");
  EXPECT_EQ(Ndmspc::RenderTemplate("{{ binningName }} ( {{ levels }} )", params), "b0 ( )");
  // A step is code: its own quotes, escapes and line breaks are kept exactly as the tool wrote them.
  EXPECT_EQ(Ndmspc::RenderTemplate("f({{ name }});", {{"name", "a\"b\\c"}}, Ndmspc::NTemplateStyle::Cpp),
            "f(\"a\\\"b\\\\c\");");
  EXPECT_EQ(Ndmspc::RenderTemplate("{{ flag }}, {{ nothing }}", {{"flag", true}, {"nothing", nullptr}},
                                   Ndmspc::NTemplateStyle::Cpp),
            "true, nullptr");
}
