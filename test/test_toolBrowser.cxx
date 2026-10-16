/**
 * The browser tool, loaded and called the way the server loads and calls it.
 *
 * Covers the browsing loop: `ndmspc/browser/open` publishes the file's tree onto the
 * `ndmspc/browser/browse` step (no pad is filled), `ndmspc/browser/browse` re-publishes it, and the
 * internal `ndmspc/rbrowser/ls` / `ndmspc/rbrowser/draw` — which the tree's nodes drive — expand a
 * folder and draw an object. The internals sit in a group of their own on purpose: the server records a
 * node for every POST of an action in a combination group, and browsing must add no step.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TH1D.h>
#include <THnSparse.h>
#include <THttpCallArg.h>
#include <TNtuple.h>
#include <TSystem.h>
#include <TTree.h>

#include "ndmspc/core/NGnTree.h"
#include "ndmspc/core/NLogger.h"
#include "ndmspc/http/NInstanceTree.h"
#include "ndmspc/http/NMacroBuilder.h"

#include "NToolHarness.h"

#ifndef NDMSPC_TOOL_BROWSER_MACRO
#define NDMSPC_TOOL_BROWSER_MACRO "toolBrowser.C"
#endif

namespace {

using Ndmspc::Test::ToolHarness;

/// The macro under test, resolved by the test build.
constexpr const char * kToolMacro = NDMSPC_TOOL_BROWSER_MACRO;

/// The file the tests browse: a histogram and a tree at the top level, and one inside a folder.
const char * kFile = "browser_test.root";

/// Write the fixture file once, before any test opens it.
void WriteTestFile()
{
  TFile * file = TFile::Open(kFile, "RECREATE");
  if (file == nullptr || file->IsZombie()) return;

  TH1D top("hpx", "hpx;x;counts", 10, 0.0, 1.0);
  top.Fill(0.5);
  top.Write();

  TNtuple ntuple("ntuple", "ntuple", "px:py:pz");
  ntuple.Fill(1.0f, 2.0f, 3.0f);
  ntuple.Write();

  // The NGNT tree: an NGNT analysis keeps its state in a tree of this name. It browses like any tree and
  // carries a badge that opens the file in the tool that walks it (see `TheNgntTreeCarriesABadge…`).
  TTree ngnt("ngnt", "ngnt;an NGNT analysis");
  Int_t ngntValue = 1;
  ngnt.Branch("value", &ngntValue, "value/I");
  ngnt.Fill();
  ngnt.Write();

  TDirectory * dir = file->mkdir("sub");
  if (dir != nullptr) {
    dir->cd();
    TH1D inner("hsub", "hsub;x;counts", 5, 0.0, 1.0);
    inner.Fill(0.25);
    inner.Write();
    file->cd();
  }

  // A THnSparse: the object the browser cannot draw directly, so its click opens a projection dialog.
  const Int_t    nbins[3] = {20, 8, 4};
  const Double_t xmin[3]  = {0.0, 0.0, 0.0};
  const Double_t xmax[3]  = {2.0, 4.0, 40.0};
  THnSparseD     sparse("hns", "hns", 3, nbins, xmin, xmax);
  sparse.GetAxis(0)->SetName("mass");
  sparse.GetAxis(0)->SetTitle("invariant mass");
  sparse.GetAxis(1)->SetName("pt");
  sparse.GetAxis(1)->SetTitle("p_{T}");
  sparse.GetAxis(2)->SetName("ce");
  sparse.GetAxis(2)->SetTitle("centrality");
  const Double_t a[3] = {0.5, 1.0, 10.0};
  const Double_t b[3] = {1.5, 3.0, 30.0};
  sparse.Fill(a, 3.0);
  sparse.Fill(b, 2.0);
  sparse.Write();

  // A second object with the same axes (another spectrum of one analysis): a projection configuration
  // saved for one is reused for the other.
  THnSparseD sparse2("hns2", "hns2", 3, nbins, xmin, xmax);
  sparse2.GetAxis(0)->SetName("mass");
  sparse2.GetAxis(0)->SetTitle("invariant mass");
  sparse2.GetAxis(1)->SetName("pt");
  sparse2.GetAxis(1)->SetTitle("p_{T}");
  sparse2.GetAxis(2)->SetName("ce");
  sparse2.GetAxis(2)->SetTitle("centrality");
  sparse2.Fill(a, 1.0);
  sparse2.Write();

  // A sparse with a **categorical** axis (a label per bin): it cannot be rebinned, so the import tab turns
  // its rebin cell off rather than taking a number the importer would refuse.
  const Int_t    catBins[1] = {3};
  const Double_t catMin[1]  = {0.0};
  const Double_t catMax[1]  = {3.0};
  THnSparseF     categorical("hcat", "hcat", 1, catBins, catMin, catMax);
  categorical.GetAxis(0)->SetName("type");
  categorical.GetAxis(0)->SetBinLabel(1, "a");
  categorical.GetAxis(0)->SetBinLabel(2, "b");
  categorical.GetAxis(0)->SetBinLabel(3, "c");
  categorical.Fill(0.5);
  categorical.Write();

  // A sparse whose data lies outside its axis' bounds (in the overflow). ROOT folds the under/overflow
  // bins into the first and last bin while no range is set, so projecting it must keep them — a range
  // that covers the whole axis (what the dialog's untouched min/max is) must not silently become a cut
  // and empty the projection.
  const Int_t    ovBins[1] = {4};
  const Double_t ovMin[1]  = {0.0};
  const Double_t ovMax[1]  = {4.0};
  THnSparseD     overflow("hov", "hov", 1, ovBins, ovMin, ovMax);
  overflow.GetAxis(0)->SetName("ncl");
  overflow.GetAxis(0)->SetTitle("NCl");
  const Double_t away[1] = {10.0};
  overflow.Fill(away, 7.0);
  overflow.Write();

  file->Close();
  delete file;
}

/// The first node of a tree value whose label is `label`, or nullptr.
const json * FindNode(const json & nodes, const std::string & label)
{
  for (const auto & node : nodes) {
    if (node.value("label", std::string()) == label) return &node;
  }
  return nullptr;
}

/// The `axes` table the projection dialog sends for the 3-axis `hns` fixture: one row per axis, the
/// axes in `used` ticked. min/max/rebin are omitted, so the handler's defaults apply.
json AxesTable(const std::vector<int> & used)
{
  json rows = json::array();
  for (int i = 0; i < 3; i++) {
    const bool on = std::find(used.begin(), used.end(), i) != used.end();
    rows.push_back(json{{"use", on}});
  }
  return rows;
}

/// How many times `needle` occurs in `text` (a canvas' serialized JSON names each histogram once).
int CountOf(const std::string & text, const std::string & needle)
{
  int    count = 0;
  size_t at    = 0;
  while ((at = text.find(needle, at)) != std::string::npos) {
    count++;
    at += needle.size();
  }
  return count;
}

/// The total of a projected pad's bins (`fArray`), or 0 when it carries none — what tells a projection
/// that drew the sparse's data from one that came out empty.
double BinTotal(const json & pad)
{
  const json & value = pad["value"];
  if (!value.contains("fArray") || !value["fArray"].is_array()) return 0.0;
  double total = 0.0;
  for (const auto & bin : value["fArray"]) {
    if (bin.is_number()) total += bin.get<double>();
  }
  return total;
}

class ToolBrowser : public ::testing::Test {
  protected:
  static void SetUpTestSuite() { WriteTestFile(); }
  void SetUp() override { fTool = std::make_unique<ToolHarness>(kToolMacro); }

  /** One request through the same dispatch `/api/*` uses — what records a combination node — as its reply. */
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

TEST_F(ToolBrowser, RegistersTheBrowseStepAndItsHiddenInternals)
{
  for (const char * key : {"ndmspc/browser/open", "ndmspc/browser/browse", "ndmspc/rbrowser/ls", "ndmspc/rbrowser/draw",
                           "ndmspc/rbrowser/sparse", "ndmspc/rbrowser/project"}) {
    EXPECT_NE(fTool->Handler(key), nullptr) << key << " did not register a handler";
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(key);
    ASSERT_NE(info, nullptr) << key << " registered no metadata";
    EXPECT_TRUE(info->inputSchema.contains("properties")) << key << " has no properties";
  }

  // `open` starts the combination; `browse` hangs under it, and its form is the file tree.
  const Ndmspc::NMcpToolInfo * open = fTool->ToolInfo("ndmspc/browser/open");
  EXPECT_TRUE(open->dependsOn.empty()) << "open is the head of the chain";
  EXPECT_EQ(open->label, "{{ file }}");
  EXPECT_EQ(open->order, 1);
  EXPECT_EQ(open->inputSchema["properties"]["file"].value("default", std::string()),
            "https://root.cern/js/files/hsimple.root");
  // The tool names no pad: the pad view routes what it shows.
  EXPECT_FALSE(open->inputSchema["properties"].contains("pad"));

  const Ndmspc::NMcpToolInfo * browse = fTool->ToolInfo("ndmspc/browser/browse");
  ASSERT_EQ(browse->dependsOn.size(), 1u);
  EXPECT_EQ(browse->dependsOn[0], "ndmspc/browser/open");
  EXPECT_EQ(browse->inputSchema["properties"]["key"].value("format", std::string()), "tree");
  EXPECT_FALSE(browse->inputSchema["properties"].contains("pad"));

  // The internals are a plain, hidden group: they add no step, and neither list shows them.
  for (const char * key : {"ndmspc/rbrowser/ls", "ndmspc/rbrowser/draw", "ndmspc/rbrowser/sparse", "ndmspc/rbrowser/project"}) {
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(key);
    EXPECT_TRUE(info->hidden) << key << " should be hidden";
    EXPECT_TRUE(info->dependsOn.empty()) << key << " should add no step";
  }
}

/// What a combination of this tool reads as when it is handed on as a macro (`ndmspc/macro`): the file it
/// opened and the object its browse step is on — the *object*, which is what somebody going on in ROOT
/// needs, not a drawing of it. The type is the class the step recorded (see the recording test below), so a
/// tree comes out a `TTree *` rather than an anonymous `TObject *`.
TEST_F(ToolBrowser, ACombinationOfItReadsAsTheFileAndTheObjectItIsOn)
{
  ASSERT_NE(fTool->ToolInfo("ndmspc/browser/open"), nullptr);
  EXPECT_EQ(fTool->ToolInfo("ndmspc/browser/open")->macroLines,
            std::vector<std::string>({"auto * file = TFile::Open({{ file }});"}));
  EXPECT_EQ(fTool->ToolInfo("ndmspc/browser/open")->macroIncludes, std::vector<std::string>({"TFile.h"}));
  ASSERT_NE(fTool->ToolInfo("ndmspc/browser/browse"), nullptr);
  EXPECT_EQ(fTool->ToolInfo("ndmspc/browser/browse")->macroLines,
            std::vector<std::string>({"auto * object = ({{ state.cls:bare }} *) file->Get({{ state.key }});"}));
  EXPECT_EQ(fTool->ToolInfo("ndmspc/browser/browse")->label, "{{ state.key }}"); // the step is named by the pick

  // A histogram picked in the tree, and the file it lives in. The pick is the step's own *state* (what it
  // recorded, `{{ state.… }}`), because a form never sends it.
  const Ndmspc::NMacroDocument object = Ndmspc::BuildCombinationMacro(
      {{"ndmspc/browser/open", {{"file", "cernstaff.root"}}, json::object()},
       {"ndmspc/browser/browse", {{"key", "hpx"}}, {{"key", "hpx"}, {"cls", "TH1D"}}}},
      "browse 1");
  EXPECT_EQ(object.text, "// browse 1: the room's active combination\n"
                         "// Written read-only - nothing in the room was changed.\n"
                         "\n"
                         "#include <TFile.h>\n"
                         "\n"
                         "void browse_1()\n"
                         "{\n"
                         "  auto * file = TFile::Open(\"cernstaff.root\");\n"
                         "  auto * object = (TH1D *) file->Get(\"hpx\");\n"
                         "}\n");

  // A tree picked in the tree — including a click on one of its branches, which is the tree that is held:
  // "we just need the TTree object and that is it".
  const Ndmspc::NMacroDocument tree = Ndmspc::BuildCombinationMacro(
      {{"ndmspc/browser/open", {{"file", "cernstaff.root"}}, json::object()},
       {"ndmspc/browser/browse", {{"key", "ntuple"}}, {{"key", "ntuple"}, {"cls", "TNtuple"}}}},
      "browse 1");
  EXPECT_NE(tree.text.find("  auto * object = (TNtuple *) file->Get(\"ntuple\");\n"), std::string::npos)
      << tree.text;
  EXPECT_EQ(tree.text.find("Draw"), std::string::npos) << tree.text; // the macro makes objects, it does not use them
}

/**
 * A pick leaves the object on the browse step, bringing that step into being if it was not there.
 *
 * A click adds no step — it runs the hidden `ndmspc/rbrowser/draw` — so the pick belongs to the browse step of
 * the group's live chain; and since that step has no Run button (its form *is* the tree), the first pick is
 * what creates it, under the `open` it browses. The combination then holds the step, named by the object it is
 * on, and a macro written from it carries the object. This drives the server's own dispatch, because that is
 * what records a node.
 */
TEST_F(ToolBrowser, APickCreatesTheBrowseStepAndMovesItThereafter)
{
  Ndmspc::NHttpServer * server = fTool->Server();

  const json opened = Dispatch(server, "POST", "ndmspc/browser/open", {{"file", kFile}});
  ASSERT_EQ(opened["result"], "success") << opened.dump();
  const std::string openId = opened["combination"]["id"];

  // Nothing picked yet: the combination is the open step alone, so a macro written from it has no object.
  {
    Ndmspc::NInstanceTree tree(server->GetCombinations());
    EXPECT_TRUE(tree.Children(openId).empty());
  }

  // The first click: the browse step appears under the open, on the object that was clicked.
  const json click = Dispatch(server, "POST", "ndmspc/rbrowser/draw",
                              {{"key", "hpx"}, {"path", json::array({openId})}});
  ASSERT_EQ(click["result"], "success") << click.dump();

  Ndmspc::NInstanceTree afterClick(server->GetCombinations());
  const std::vector<std::string> children = afterClick.Children(openId);
  ASSERT_EQ(children.size(), 1u) << afterClick.ToTree().dump();
  const std::string browseId = children.front();
  EXPECT_EQ(afterClick.Action(browseId), "ndmspc/browser/browse");
  EXPECT_EQ(afterClick.Params(browseId)["key"], "hpx"); // the form's own field, so the step can be run again
  EXPECT_EQ(afterClick.State(browseId)["key"], "hpx");  // the pick, which is what the macro and the label read
  EXPECT_EQ(afterClick.State(browseId)["cls"], "TH1D");
  EXPECT_EQ(afterClick.Get(browseId)["label"], "hpx"); // the step is named by the object it is on

  // The next click moves that one step — no second node, and the name follows the new object.
  const json again = Dispatch(server, "POST", "ndmspc/rbrowser/draw",
                              {{"key", "ntuple"}, {"path", json::array({openId, browseId})}});
  ASSERT_EQ(again["result"], "success") << again.dump();

  Ndmspc::NInstanceTree afterAgain(server->GetCombinations());
  EXPECT_EQ(afterAgain.Children(openId), std::vector<std::string>({browseId}));
  EXPECT_EQ(afterAgain.State(browseId)["key"], "ntuple");
  EXPECT_EQ(afterAgain.State(browseId)["cls"], "TNtuple");
  EXPECT_EQ(afterAgain.Get(browseId)["label"], "ntuple");
}

TEST_F(ToolBrowser, OpenPublishesTheFileTreeOntoTheBrowseStep)
{
  const auto call = fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success");

  // The tree belongs to the browse step, so `open` fills no pad.
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);

  const json & tree = call.ws["workspace"]["browse"]["properties"]["key"];
  EXPECT_EQ(tree.value("format", std::string()), "tree");
  ASSERT_TRUE(tree["nodes"].is_array());

  const json * hpx = FindNode(tree["nodes"], "hpx;1");
  ASSERT_NE(hpx, nullptr) << "the top-level histogram is missing from the tree";
  EXPECT_EQ((*hpx)["detail"], "TH1D");
  EXPECT_EQ((*hpx)["action"]["path"], "ndmspc/rbrowser/draw");
  EXPECT_EQ((*hpx)["action"]["payload"]["key"], "hpx");
  // The tree's pad must not ride along on the draw action, or the object lands beside the tree.
  EXPECT_FALSE((*hpx)["action"]["payload"].contains("pad"));

  const json * sub = FindNode(tree["nodes"], "sub;1");
  ASSERT_NE(sub, nullptr) << "the folder is missing from the tree";
  EXPECT_EQ((*sub)["expandable"], true);
  EXPECT_EQ((*sub)["action"]["path"], "ndmspc/rbrowser/ls");
}

/// The NGNT tree stays an **ordinary tree** — it expands to its branches like any other — and carries a
/// **badge** beside it: a click on the badge opens the same file in the NGNT explorer. The badge's action
/// is a **root** one, so the client leaves the browser's combination off it (`ndmspc/ngnt/open` starts a
/// session of its own). A plain tree carries no badge.
TEST_F(ToolBrowser, TheNgntTreeCarriesABadgeToTheNgntExplorer)
{
  const auto call = fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success");

  const json & nodes = call.ws["workspace"]["browse"]["properties"]["key"]["nodes"];
  const json * ngnt  = FindNode(nodes, "ngnt;1");
  ASSERT_NE(ngnt, nullptr) << "the ngnt tree is missing from the tree";

  // Still a tree: it expands (its own click draws it, the chevron loads its branches) ...
  EXPECT_EQ((*ngnt)["expandable"], true);
  EXPECT_EQ((*ngnt)["action"]["path"], "ndmspc/rbrowser/draw");
  EXPECT_EQ((*ngnt)["loadAction"]["path"], "ndmspc/rbrowser/ls");

  // ... and carries the badge: the button to the tool that walks the file.
  ASSERT_TRUE(ngnt->contains("badge")) << "the ngnt tree carries no badge";
  const json & badge = (*ngnt)["badge"];
  EXPECT_EQ(badge.value("label", std::string()), "Open in NGNT explorer");
  EXPECT_EQ(badge["action"].value("method", std::string()), "POST");
  EXPECT_EQ(badge["action"].value("path", std::string()), "ndmspc/ngnt/open");
  EXPECT_EQ(badge["action"]["payload"].value("file", std::string()), kFile);
  EXPECT_EQ(badge["action"].value("root", false), true) << "the redirect starts a combination of its own";

  // Expanding it lists its branches, as any tree does.
  const auto   expanded = fTool->Call("ndmspc/rbrowser/ls", "POST", json{{"key", "ngnt"}});
  const json * opened   = FindNode(expanded.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "ngnt;1");
  ASSERT_NE(opened, nullptr);
  ASSERT_TRUE(opened->contains("children")) << "the ngnt tree did not list its branches";
  EXPECT_GE((*opened)["children"].size(), 1u);

  // A plain tree is untouched: no badge.
  const json * ntuple = FindNode(nodes, "ntuple;1");
  ASSERT_NE(ntuple, nullptr);
  EXPECT_FALSE(ntuple->contains("badge"));
}

TEST_F(ToolBrowser, LsSendsTheTreeWithTheFolderExpanded)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto call = fTool->Call("ndmspc/rbrowser/ls", "POST", json{{"key", "sub"}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success");
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);

  const json * sub = FindNode(call.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "sub;1");
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ((*sub)["loaded"], true);
  ASSERT_TRUE((*sub).contains("children"));
  ASSERT_EQ((*sub)["children"].size(), 1u);
  EXPECT_EQ((*sub)["children"][0]["label"], "hsub;1");
}

TEST_F(ToolBrowser, ATreeDrawsAndExpandsToItsBranches)
{
  // ROOT reports a TNtuple key as a "folder"; it must still be a leaf for drawing and expand to its
  // branches (this is the regression that sent it down the directory path and hid them).
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto expanded = fTool->Call("ndmspc/rbrowser/ls", "POST", json{{"key", "ntuple"}});

  const json * nt =
      FindNode(expanded.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "ntuple;1");
  ASSERT_NE(nt, nullptr) << "the tree is missing from the tree";
  EXPECT_EQ((*nt)["action"]["path"], "ndmspc/rbrowser/draw");
  EXPECT_EQ((*nt)["expandable"], true);
  ASSERT_TRUE((*nt).contains("children"));
  EXPECT_GE((*nt)["children"].size(), 3u);

  // A branch draws itself: clicking it projects the branch into a histogram.
  const json & px = (*nt)["children"][0];
  EXPECT_EQ(px["action"]["path"], "ndmspc/rbrowser/draw");
  EXPECT_EQ(px["action"]["payload"]["branch"], "px");

  const auto drawn     = fTool->Call("ndmspc/rbrowser/draw", "POST", px["action"]["payload"]);
  const json drawnPads = ToolHarness::Pads(drawn);
  ASSERT_EQ(drawnPads.size(), 1u);
  EXPECT_EQ(drawnPads[0].value("kind", std::string()), "jsroot");
  // No pad is named: the pad view decides where it lands.
  EXPECT_TRUE(drawnPads[0].value("pad", std::string()).empty());
  EXPECT_EQ(drawnPads[0]["value"]["fName"], "px");
}

TEST_F(ToolBrowser, DrawTurnsAKeyIntoAJsrootEnvelope)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto call = fTool->Call("ndmspc/rbrowser/draw", "POST", json{{"key", "hpx"}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success");

  const json pads = ToolHarness::Pads(call);
  ASSERT_EQ(pads.size(), 1u);
  EXPECT_EQ(pads[0].value("kind", std::string()), "jsroot");
  EXPECT_TRUE(pads[0].value("pad", std::string()).empty());
  EXPECT_EQ(pads[0]["value"]["fName"], "hpx");
}

TEST_F(ToolBrowser, ANodeActionNamesNoPadSoTheViewRoutesIt)
{
  const auto   open = fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const json * hpx =
      FindNode(open.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "hpx;1");
  ASSERT_NE(hpx, nullptr);
  const json payload = (*hpx)["action"]["payload"];
  EXPECT_FALSE(payload.contains("pad"));

  // Exactly what a click in the browse step's tree would make: the node's own payload.
  const auto draw      = fTool->Call("ndmspc/rbrowser/draw", "POST", payload);
  const json drawnPads = ToolHarness::Pads(draw);
  ASSERT_EQ(drawnPads.size(), 1u);
  // The tool names no pad, so the pad view decides (fixed pad, or the rotating ones).
  EXPECT_TRUE(drawnPads[0].value("pad", std::string()).empty());
}

TEST_F(ToolBrowser, DrawingWithoutAnOpenFileIsRefused)
{
  const auto call = fTool->Call("ndmspc/rbrowser/draw", "POST", json{{"key", "hpx"}});
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);
  EXPECT_FALSE(call.reply.value("error", std::string()).empty());
}

TEST_F(ToolBrowser, DeleteClosesTheFile)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  ASSERT_NE(fTool->Objects().find("browserFile"), fTool->Objects().end());

  const auto call = fTool->Call("ndmspc/browser/open", "DELETE");
  EXPECT_EQ(call.reply.value("result", std::string()), "success");
  EXPECT_EQ(fTool->Objects().find("browserFile"), fTool->Objects().end());
}

TEST_F(ToolBrowser, ASparseNodeOpensAProjectionDialog)
{
  const auto open = fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const json * hns =
      FindNode(open.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "hns;1");
  ASSERT_NE(hns, nullptr) << "the THnSparse is missing from the tree";
  // jsroot cannot draw a sparse, so the node is a leaf whose click opens the dialog rather than drawing.
  EXPECT_FALSE((*hns).value("expandable", false));
  EXPECT_EQ((*hns)["action"]["path"], "ndmspc/rbrowser/sparse");
  EXPECT_EQ((*hns)["action"]["payload"]["key"], "hns");

  const auto call = fTool->Call("ndmspc/rbrowser/sparse", "POST", (*hns)["action"]["payload"]);
  ASSERT_EQ(call.reply.value("result", std::string()), "success");
  // The dialog is the message: nothing is drawn until the form is submitted.
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);

  const json & dialog = call.ws["payload"]["dialog"];
  // The dialog is titled by the object; what is done with it is what its tabs say (see
  // `TheSparseDialogOffersProjectAndImportTabs`).
  EXPECT_EQ(dialog.value("title", std::string()), "hns");
  EXPECT_EQ(dialog["action"]["path"], "ndmspc/rbrowser/project");
  EXPECT_EQ(dialog["action"]["payload"]["key"], "hns");
  // The fields read name-first, then the axes (a JSON object's keys would come out sorted).
  ASSERT_TRUE(dialog["schema"].contains("order"));
  EXPECT_EQ(dialog["schema"]["order"][0], "name");

  const json & props = dialog["schema"]["properties"];
  ASSERT_TRUE(props.contains("axes")) << "the axes table is missing from the dialog";
  EXPECT_EQ(props["axes"].value("format", std::string()), "table");
  EXPECT_TRUE(props.contains("drawOpts"));
  // The projection's name defaults to "projection" — its tab and the histogram's name.
  ASSERT_TRUE(props.contains("name")) << "the name field is missing from the dialog";
  EXPECT_EQ(props["name"].value("default", std::string()), "projection");
  // No canvas yet, so SAME starts unticked — there is nothing to overlay onto.
  EXPECT_EQ(props["same"].value("default", true), false);

  // The table has a checkbox column (`use`), the axis as its own index and name, and the options
  // after it; the first axis is ticked by default, so the form's first run is a 1D projection.
  ASSERT_TRUE(props["axes"]["columns"].is_array());
  EXPECT_EQ(props["axes"]["columns"][0].value("key", std::string()), "use");
  EXPECT_EQ(props["axes"]["columns"][1].value("key", std::string()), "id");
  EXPECT_EQ(props["axes"]["columns"][2].value("key", std::string()), "axis");
  // The name is rendered as plain text: not TLatex, which would read the underscore of a name like
  // `p_T` as markup. What a reader would call the axis travels beside it, for the cell's hover.
  EXPECT_FALSE(props["axes"]["columns"][2].contains("format"));
  EXPECT_EQ(props["axes"]["columns"][2].value("hint", std::string()), "axisTitle");

  // One row per axis: the index it projects at, its name, and the name's title for the hover.
  ASSERT_TRUE(props["axes"]["default"].is_array());
  ASSERT_EQ(props["axes"]["default"].size(), 3u);
  EXPECT_EQ(props["axes"]["default"][0].value("id", -1), 0);
  EXPECT_EQ(props["axes"]["default"][0].value("axis", std::string()), "mass");
  EXPECT_EQ(props["axes"]["default"][1].value("id", -1), 1);
  EXPECT_EQ(props["axes"]["default"][1].value("axis", std::string()), "pt");
  EXPECT_EQ(props["axes"]["default"][0].value("axisTitle", std::string()), "invariant mass");
  EXPECT_EQ(props["axes"]["default"][0].value("use", false), true);
  EXPECT_EQ(props["axes"]["default"][1].value("use", false), false);
}

TEST_F(ToolBrowser, ProjectingASparseDrawsTheChosenAxes)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // 1D: one axis ticked in the table.
  const auto one =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  ASSERT_EQ(one.reply.value("result", std::string()), "success");
  const json onePads = ToolHarness::Pads(one);
  ASSERT_EQ(onePads.size(), 1u);
  EXPECT_EQ(onePads[0].value("kind", std::string()), "jsroot");
  // No name given: the projection is named after the default ("projection").
  EXPECT_EQ(onePads[0]["value"]["fName"], "projection");
  EXPECT_EQ(onePads[0]["value"].value("_typename", std::string()).rfind("TH1", 0), 0u);
  // And it carries the data — a projection of a sparse that came back empty would not.
  EXPECT_GT(BinTotal(onePads[0]), 0.0);

  // 2D: two axes.
  const auto two =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0, 1})}});
  const json twoPads = ToolHarness::Pads(two);
  ASSERT_EQ(twoPads.size(), 1u);
  EXPECT_EQ(twoPads[0]["value"].value("_typename", std::string()).rfind("TH2", 0), 0u);
  EXPECT_GT(BinTotal(twoPads[0]), 0.0);

  // 3D: three axes.
  const auto three =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0, 1, 2})}});
  const json threePads = ToolHarness::Pads(three);
  ASSERT_EQ(threePads.size(), 1u);
  EXPECT_EQ(threePads[0]["value"].value("_typename", std::string()).rfind("TH3", 0), 0u);
  EXPECT_GT(BinTotal(threePads[0]), 0.0);
}

TEST_F(ToolBrowser, ProjectingWithoutAnAxisIsRefused)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto call =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({})}});
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);
  EXPECT_FALSE(call.reply.value("error", std::string()).empty());
}

TEST_F(ToolBrowser, ASparseRangeNarrowsTheProjection)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const json axes = json::array({
      {{"use", true}, {"min", 0.2}, {"max", 1.0}},
      {{"use", false}},
      {{"use", false}},
  });
  const auto call = fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", axes}});
  const json pads = ToolHarness::Pads(call);
  ASSERT_EQ(pads.size(), 1u);
  // Axis 0 has 20 bins over [0,2]; [0.2,1.0] is a fraction of it.
  ASSERT_TRUE(pads[0]["value"].contains("fXaxis"));
  EXPECT_LT(pads[0]["value"]["fXaxis"].value("fNbins", 0), 20);
}

TEST_F(ToolBrowser, ASparseRangeOnANonProjectedAxisNarrowsTheProjection)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  const auto full =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});

  // Axis 1 (pt) is not projected, but a range on it must still carve the sample the mass projection is
  // built from: point b has pt 3, so a [0,2] cut leaves only point a.
  const json axes = json::array({
      {{"use", true}},
      {{"use", false}, {"min", 0.0}, {"max", 2.0}},
      {{"use", false}},
  });
  const auto cut = fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", axes}});

  const json fullPads = ToolHarness::Pads(full);
  const json cutPads  = ToolHarness::Pads(cut);
  ASSERT_EQ(fullPads.size(), 1u);
  ASSERT_EQ(cutPads.size(), 1u);
  // Mass 1.5 (point b) sits in X bin 16 — `fArray[16]`, after the underflow cell. The pt cut must
  // empty it, while the full projection keeps its weight.
  ASSERT_TRUE(fullPads[0]["value"].contains("fArray"));
  ASSERT_TRUE(cutPads[0]["value"].contains("fArray"));
  EXPECT_GT(fullPads[0]["value"]["fArray"][16].get<double>(), 0.0);
  EXPECT_DOUBLE_EQ(cutPads[0]["value"]["fArray"][16].get<double>(), 0.0);
}

TEST_F(ToolBrowser, ASparseRebinCoarsensTheProjection)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto plain =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  const json axes =
      json::array({{{"use", true}, {"rebin", 2}}, {{"use", false}}, {{"use", false}}});
  const auto rebinned =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", axes}});
  const json plainPads = ToolHarness::Pads(plain);
  const json rebinPads = ToolHarness::Pads(rebinned);
  ASSERT_EQ(rebinPads.size(), 1u);
  ASSERT_TRUE(rebinPads[0]["value"].contains("fXaxis"));
  // Axis 0 has 20 bins; rebinning by two leaves ten.
  EXPECT_EQ(rebinPads[0]["value"]["fXaxis"].value("fNbins", 0),
            plainPads[0]["value"]["fXaxis"].value("fNbins", 0) / 2);
}

TEST_F(ToolBrowser, ASparseProjectionConfigurationIsReused)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // Project hns on axis 1 with a range and "same canvas", which keeps that configuration.
  const json axes = json::array({
      {{"use", false}},
      {{"use", true}, {"min", 1.0}, {"max", 3.0}, {"rebin", 2}},
      {{"use", false}},
  });
  const auto first = fTool->Call("ndmspc/rbrowser/project", "POST",
                                 json{{"key", "hns"}, {"axes", axes}, {"same", true}});
  ASSERT_EQ(first.reply.value("result", std::string()), "success");
  // The default name is the canvas: `projection`.
  const json firstPads = ToolHarness::Pads(first);
  ASSERT_EQ(firstPads.size(), 1u);
  EXPECT_EQ(firstPads[0].value("label", std::string()), "projection");

  // Reopening the dialog opens on the saved configuration ...
  const auto   again = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  const json & props = again.ws["payload"]["dialog"]["schema"]["properties"];
  const json & rows  = props["axes"]["default"];
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].value("use", true), false);
  EXPECT_EQ(rows[1].value("use", false), true);
  EXPECT_DOUBLE_EQ(rows[1].value("min", 0.0), 1.0);
  EXPECT_DOUBLE_EQ(rows[1].value("max", 0.0), 3.0);
  EXPECT_EQ(rows[1].value("rebin", 0), 2);
  EXPECT_EQ(props["same"].value("default", false), true);

  // ... and so does a different object with the same axes.
  const auto   other     = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns2"}});
  const json & otherRows = other.ws["payload"]["dialog"]["schema"]["properties"]["axes"]["default"];
  ASSERT_EQ(otherRows.size(), 3u);
  EXPECT_EQ(otherRows[1].value("use", false), true);
  EXPECT_DOUBLE_EQ(otherRows[1].value("max", 0.0), 3.0);
}

TEST_F(ToolBrowser, ASparseSameCanvasOverlaysTheProjections)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // Projections share a canvas by name — `cmp` is that canvas here.
  const auto one =
      fTool->Call("ndmspc/rbrowser/project", "POST",
                  json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "cmp"}});
  const json onePads = ToolHarness::Pads(one);
  ASSERT_EQ(onePads.size(), 1u);
  EXPECT_EQ(onePads[0].value("label", std::string()), "cmp");
  // The pad is told to drop its previous drawing, so the canvas replaces it rather than stacking.
  EXPECT_EQ(onePads[0].value("replace", false), true);
  ASSERT_TRUE(onePads[0]["value"].contains("_typename"));
  EXPECT_EQ(onePads[0]["value"].value("_typename", std::string()), "TCanvas");
  EXPECT_EQ(CountOf(onePads[0]["value"].dump(), "TH1D"), 1);

  // A second one is overlaid on it by ROOT: one canvas holding both histograms.
  const auto two =
      fTool->Call("ndmspc/rbrowser/project", "POST",
                  json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "cmp"}});
  const json twoPads = ToolHarness::Pads(two);
  ASSERT_EQ(twoPads.size(), 1u);
  EXPECT_EQ(twoPads[0]["value"].value("_typename", std::string()), "TCanvas");
  const std::string twoText = twoPads[0]["value"].dump();
  EXPECT_EQ(CountOf(twoText, "TH1D"), 2);
  // Both projections are on the canvas, in the order they were added, not just the last.
  EXPECT_NE(twoText.find("hns2"), std::string::npos);

  // The same projection again (the same object, axes and options — what a replayed step is) is not
  // drawn twice: the canvas still holds two curves.
  const auto again =
      fTool->Call("ndmspc/rbrowser/project", "POST",
                  json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "cmp"}});
  const json againPads = ToolHarness::Pads(again);
  ASSERT_EQ(againPads.size(), 1u);
  EXPECT_EQ(againPads[0]["value"].value("_typename", std::string()), "TCanvas");
  EXPECT_EQ(CountOf(againPads[0]["value"].dump(), "TH1D"), 2);

  // A *different* projection of an object — here a narrower range — is a curve of its own.
  const json ranged = json::array({
      {{"use", true}, {"min", 0.2}, {"max", 1.0}},
      {{"use", false}},
      {{"use", false}},
  });
  const auto third =
      fTool->Call("ndmspc/rbrowser/project", "POST",
                  json{{"key", "hns"}, {"axes", ranged}, {"same", true}, {"name", "cmp"}});
  const json thirdPads = ToolHarness::Pads(third);
  ASSERT_EQ(thirdPads.size(), 1u);
  EXPECT_EQ(CountOf(thirdPads[0]["value"].dump(), "TH1D"), 3);
}

TEST_F(ToolBrowser, ASparseNamedCanvasesAreKeptApart)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // Two canvases, by name; each keeps its own curves.
  fTool->Call("ndmspc/rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "A"}});
  fTool->Call("ndmspc/rbrowser/project", "POST",
              json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "B"}});

  // A grows ...
  const auto a = fTool->Call(
      "ndmspc/rbrowser/project", "POST",
      json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "A"}});
  const json aPads = ToolHarness::Pads(a);
  ASSERT_EQ(aPads.size(), 1u);
  EXPECT_EQ(aPads[0].value("label", std::string()), "A");
  EXPECT_EQ(CountOf(aPads[0]["value"].dump(), "TH1D"), 2);

  // ... and B is untouched by it.
  const auto b = fTool->Call(
      "ndmspc/rbrowser/project", "POST",
      json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "B"}});
  const json bPads = ToolHarness::Pads(b);
  ASSERT_EQ(bPads.size(), 1u);
  EXPECT_EQ(bPads[0].value("label", std::string()), "B");
  EXPECT_EQ(CountOf(bPads[0]["value"].dump(), "TH1D"), 1);
}

TEST_F(ToolBrowser, ASparseSameCanvasIncludesEarlierProjections)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // One projection drawn on its own (Same canvas off) is still remembered for the file ...
  const auto own =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  const json ownPads = ToolHarness::Pads(own);
  ASSERT_EQ(ownPads.size(), 1u);
  EXPECT_EQ(ownPads[0].value("label", std::string()), "projection");

  // ... so turning Same canvas on shows it together with the next one.
  const auto both = fTool->Call("ndmspc/rbrowser/project", "POST",
                                json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}});
  const json pads = ToolHarness::Pads(both);
  ASSERT_EQ(pads.size(), 1u);
  EXPECT_EQ(pads[0].value("label", std::string()), "projection");
  EXPECT_EQ(CountOf(pads[0]["value"].dump(), "TH1D"), 2);
}

TEST_F(ToolBrowser, ASparseSameCanvasOffStartsANewCanvas)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  fTool->Call("ndmspc/rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}});

  // Same canvas off draws the projection as its own object — and starts a new canvas, forgetting the
  // one before it.
  const auto own =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns2"}, {"axes", AxesTable({0})}});
  const json ownPads = ToolHarness::Pads(own);
  ASSERT_EQ(ownPads.size(), 1u);
  EXPECT_EQ(ownPads[0].value("label", std::string()), "projection");

  // The canvas in hand now holds only that one: overlaying the same object again does not bring back
  // the projection the new canvas forgot.
  const auto after = fTool->Call("ndmspc/rbrowser/project", "POST",
                                 json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}});
  const json afterPads = ToolHarness::Pads(after);
  ASSERT_EQ(afterPads.size(), 1u);
  EXPECT_EQ(CountOf(afterPads[0]["value"].dump(), "TH1D"), 1);
}

TEST_F(ToolBrowser, ASparseSameChoiceIsRemembered)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // Off to begin with — a projection drawn as its own object starts the canvas.
  const auto first = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(first.ws["payload"]["dialog"]["schema"]["properties"]["same"].value("default", true), false);

  // Ticked, it stays ticked for the next dialog.
  fTool->Call("ndmspc/rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}});
  const auto ticked = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(ticked.ws["payload"]["dialog"]["schema"]["properties"]["same"].value("default", false), true);

  // Unticked, it stays unticked — and starts another canvas.
  fTool->Call("ndmspc/rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", false}});
  const auto unticked = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(unticked.ws["payload"]["dialog"]["schema"]["properties"]["same"].value("default", true), false);
}

TEST_F(ToolBrowser, ASparseNameIsRemembered)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // "projection" to begin with ...
  const auto first = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(first.ws["payload"]["dialog"]["schema"]["properties"]["name"].value("default", std::string()),
            "projection");

  // ... and the last name used comes back with the rest of the configuration.
  fTool->Call("ndmspc/rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"name", "MyCanvas"}});
  const auto again = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(again.ws["payload"]["dialog"]["schema"]["properties"]["name"].value("default", std::string()),
            "MyCanvas");
}

TEST_F(ToolBrowser, ASparseIsRereadAfterEachProjection)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // Every request frees the sparse it read, so the next one reads it again: two projections and a
  // dialog all work on the same object — and each projection still carries its data.
  const auto a =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  EXPECT_EQ(a.reply.value("result", std::string()), "success");
  const json aPads = ToolHarness::Pads(a);
  ASSERT_EQ(aPads.size(), 1u);
  EXPECT_GT(BinTotal(aPads[0]), 0.0);

  const auto b =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({1})}});
  EXPECT_EQ(b.reply.value("result", std::string()), "success");
  const json bPads = ToolHarness::Pads(b);
  ASSERT_EQ(bPads.size(), 1u);
  EXPECT_GT(BinTotal(bPads[0]), 0.0);

  const auto dialog = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(dialog.reply.value("result", std::string()), "success");
  EXPECT_TRUE(dialog.ws["payload"]["dialog"].contains("schema"));
}

TEST_F(ToolBrowser, ASparseOverlayIsCapped)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // Twelve distinct projections onto one name: the canvas keeps the last ten and drops the oldest.
  json last;
  for (int i = 0; i < 12; i++) {
    const json axes = json::array({
        {{"use", true}, {"min", 0.0}, {"max", 0.05 * (i + 1)}},
        {{"use", false}},
        {{"use", false}},
    });
    last = ToolHarness::Pads(fTool->Call(
        "ndmspc/rbrowser/project", "POST",
        json{{"key", "hns"}, {"axes", axes}, {"same", true}, {"name", "cap"}}))[0];
  }
  EXPECT_EQ(last["value"].value("_typename", std::string()), "TCanvas");
  EXPECT_EQ(CountOf(last["value"].dump(), "TH1D"), 10);
}

TEST_F(ToolBrowser, ASparseProjectionWithoutSameCanvasKeepsItsOwnTab)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto call =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  const json pads = ToolHarness::Pads(call);
  ASSERT_EQ(pads.size(), 1u);
  EXPECT_EQ(pads[0].value("label", std::string()), "projection");
}

TEST_F(ToolBrowser, ProjectingWhatTheDialogSentKeepsTheData)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  // The flow the UI drives: click the sparse (the dialog), then submit its own defaults. The dialog
  // seeds min/max from the axis' own bounds, so an untouched "project the whole axis" row still carries
  // a full range. On this fixture the data sits outside the axis (in the overflow), which ROOT only folds
  // into the first/last bin while no range is set — so turning that full range into a cut empties the
  // projection. It must not: a full range is no cut.
  const auto   dialog = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hov"}});
  const json & rows   = dialog.ws["payload"]["dialog"]["schema"]["properties"]["axes"]["default"];
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].value("use", false), true);

  const auto viaDialog =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hov"}, {"axes", rows}});
  ASSERT_EQ(viaDialog.reply.value("result", std::string()), "success");
  const json dialogPads = ToolHarness::Pads(viaDialog);
  ASSERT_EQ(dialogPads.size(), 1u);
  EXPECT_GT(BinTotal(dialogPads[0]), 0.0);

  // And it projects the same data as a request that names no range at all.
  const json none = json::array({json{{"use", true}}});
  const auto plain =
      fTool->Call("ndmspc/rbrowser/project", "POST", json{{"key", "hov"}, {"axes", none}});
  const json plainPads = ToolHarness::Pads(plain);
  ASSERT_EQ(plainPads.size(), 1u);
  EXPECT_DOUBLE_EQ(BinTotal(dialogPads[0]), BinTotal(plainPads[0]));
}

/// A THnSparse's click opens a dialog with **two tabs**: project it (as before), or import it to an
/// NGnTree. The dialog's own form and submit stay the Project tab's, so a client that knows nothing of
/// tabs behaves exactly as it did.
TEST_F(ToolBrowser, TheSparseDialogOffersProjectAndImportTabs)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto call = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success") << call.reply.dump();

  const json & dialog = call.ws["payload"]["dialog"];
  EXPECT_EQ(dialog["action"]["path"], "ndmspc/rbrowser/project");
  EXPECT_TRUE(dialog["schema"].contains("order")) << "the dialog's own form is the Project tab's";

  ASSERT_TRUE(dialog.contains("tabs")) << "the dialog offers no tabs";
  ASSERT_EQ(dialog["tabs"].size(), 2u);
  EXPECT_EQ(dialog["tabs"][0]["label"], "Project");
  EXPECT_EQ(dialog["tabs"][0]["action"]["path"], "ndmspc/rbrowser/project");
  EXPECT_EQ(dialog["tabs"][1]["label"], "Import to NGnTree");
  EXPECT_EQ(dialog["tabs"][1]["action"]["path"], "ndmspc/rbrowser/import");

  // The import tab's own settings: this object's axes, the **first** of them picked to begin with, a
  // title/rebin row per axis whose titles start as the sparse' own, and one projection to begin with —
  // built like the NGNT explorer's levels, the axes by number (see the schema's own `hint`).
  const json & props = dialog["tabs"][1]["schema"]["properties"];
  EXPECT_EQ(props["axes"]["items"]["enum"], json::array({"mass", "pt", "ce"}));
  EXPECT_EQ(props["axes"]["default"], json::array({"mass"}));
  EXPECT_EQ(props["axesTitles"]["filterBy"], "axes") << "the titles follow the axes that are picked";
  ASSERT_EQ(props["axesTitles"]["default"].size(), 3u);
  EXPECT_EQ(props["axesTitles"]["default"][0]["axis"], "mass");
  EXPECT_EQ(props["axesTitles"]["default"][0]["title"], "invariant mass");
  // The table says how big each axis is (read-only) and lets it be rebinned.
  EXPECT_EQ(props["axesTitles"]["default"][0]["bins"], 20);
  const json & columns = props["axesTitles"]["columns"];
  EXPECT_EQ(columns[1]["key"], "bins");
  EXPECT_EQ(columns[1]["readOnly"], true);
  EXPECT_EQ(columns[3]["key"], "rebin");
  ASSERT_EQ(props["projections"]["default"].size(), 1u) << "one projection to begin with";
  EXPECT_EQ(props["projections"]["default"][0], json::array({0}));
  // What the rows are called, and what each number reads as: the editor shows "Projection 1:" and picks
  // its axes by name (see the UI's levels editor).
  EXPECT_EQ(props["projections"]["groupLabel"], "Projections");
  EXPECT_EQ(props["projections"]["itemLabel"], "Projection");
  ASSERT_EQ(props["projections"]["axisLabels"].size(), 3u);
  EXPECT_EQ(props["projections"]["axisLabels"][0], "[0] mass: 20 bins");
  // A chosen axis is said by its own name, and a projection may name an axis more than once.
  EXPECT_EQ(props["projections"]["axisNames"][0], "mass");
  EXPECT_EQ(props["projections"]["unique"], false);
  EXPECT_EQ(props["outputFile"]["default"], "hns_ngnt.root");
  // The import is split over processes, the room's own CPU count to begin with.
  EXPECT_EQ(props["nProcesses"]["type"], "integer");
  EXPECT_GT(props["nProcesses"]["default"].get<int>(), 0);
  // An import takes as long as it takes, and says so: the client waits for the reply without a limit.
  EXPECT_EQ(dialog["tabs"][1]["action"]["timeoutMs"], 0);
  // The axes are spelled out where they are picked — no separate hint — and a chosen one is said by name.
  EXPECT_FALSE(dialog["tabs"][1]["schema"].contains("hint")) << "the axes are spelled out in the picker";
  ASSERT_EQ(props["axes"]["axisLabels"].size(), 3u);
  EXPECT_EQ(props["axes"]["axisLabels"][2], "[2] ce: 4 bins");
  EXPECT_EQ(props["axes"]["axisNames"][2], "ce");
}

/// The import tab's submit writes the NGnTree file the settings name, and the reply hands the client the
/// step that opens it in the NGNT explorer — the server cannot open a step, the client can.
TEST_F(ToolBrowser, TheImportTabWritesTheFileAndHandsOverTheStep)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  const std::string out = "/tmp/browser_import_test_ngnt.root";
  std::remove(out.c_str());

  const auto call = fTool->Call(
      "ndmspc/rbrowser/import", "POST",
      json{{"key", "hns"},
           {"axes", json::array({"mass", "pt"})},
           // Two rows: one for an axis that was picked (title kept, rebin 2), one for an axis that was
           // not — the pick is the authority, so the second row is not the import's.
           {"axesTitles", json::array({json{{"axis", "mass"}, {"title", "m"}, {"rebin", 2}},
                                       json{{"axis", "ce"}, {"title", "dropped"}, {"rebin", 0}}})},
           // Two projections by number, and one that names no axis of this object.
           {"projections", json::array({json::array({0}), json::array({1, 2}), json::array({9})})},
           {"outputFile", out}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success") << call.reply.dump();

  // The file is where the form said, and it holds the tree the picked axes describe.
  EXPECT_FALSE(gSystem->AccessPathName(out.c_str())) << "the import wrote no file";
  EXPECT_EQ(call.reply["outputFile"], out);

  Ndmspc::NGnTree * imported = Ndmspc::NGnTree::Open(out);
  ASSERT_NE(imported, nullptr) << "the imported file does not open as an NGnTree";
  ASSERT_NE(imported->GetBinning(), nullptr);
  ASSERT_EQ(imported->GetBinning()->GetAxes().size(), 2u);
  EXPECT_EQ(imported->GetBinning()->GetAxes()[0]->GetName(), std::string("mass"));
  EXPECT_EQ(imported->GetBinning()->GetAxes()[1]->GetName(), std::string("pt"));
  EXPECT_EQ(imported->GetBinning()->GetAxes()[0]->GetTitle(), std::string("m"));
  delete imported;

  // And the reply names the step that opens it — labelled, and with the file it wrote, so the client can
  // raise it as a notice the user acts on: what happened, what it wrote, and the step that opens it. It is
  // read from the **HTTP** envelope: that is what a websocket reply carries, `wsOut` being the broadcast.
  EXPECT_EQ(call.reply["open"]["action"], "ndmspc/ngnt/open");
  EXPECT_EQ(call.reply["open"]["params"]["file"], out);
  EXPECT_EQ(call.reply["open"]["file"], out);
  EXPECT_EQ(call.reply["open"]["what"], "Import finished");
  EXPECT_EQ(call.reply["open"]["label"], "Open in NGNT explorer");

  std::remove(out.c_str());
}

/**
 * The dialog's tabs: each says what its submit makes, and the import tab also names the action the client
 * asks for the **macro it previews** — the call the room would make, with the cfg JSON in it, so the whole
 * thing can be read and copied before a long import runs.
 */
TEST_F(ToolBrowser, TheImportTabPreviewsTheMacroItWouldRun)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  const auto dialog = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(dialog.ws["payload"]["dialog"]["tabs"][1]["macro"], "ndmspc/rbrowser/macro");

  const auto preview = fTool->Call(
      "ndmspc/rbrowser/macro", "POST",
      json{{"for", "ndmspc/rbrowser/import"},
           {"key", "hns"},
           {"axes", json::array({"mass", "pt"})},
           {"axesTitles", json::array({json{{"axis", "mass"}, {"title", "m"}, {"rebin", 2}}})},
           {"projections", json::array({json::array({0})})},
           {"nProcesses", 4},
           {"outputFile", "/tmp/preview_ngnt.root"}});
  ASSERT_EQ(preview.reply.value("result", std::string()), "success") << preview.reply.dump();

  const std::string macro = preview.reply["macro"].get<std::string>();
  // A macro that runs as it stands: the includes, the variables the run sets for parallel processing, the
  // configuration **set line by line**, and the call the room makes.
  EXPECT_NE(macro.find("#include <TSystem.h>"), std::string::npos) << macro;
  EXPECT_NE(macro.find("#include <nlohmann/json.hpp>"), std::string::npos) << macro;
  EXPECT_NE(macro.find("#include <ndmspc/core/NGnTree.h>"), std::string::npos) << macro;
  // An unnamed macro's includes go **before** its braces: a preprocessor directive does not belong inside a
  // block, and the file has to be read as it stands.
  EXPECT_LT(macro.find("#include"), macro.find("{")) << macro;
  EXPECT_NE(macro.find("#include <ndmspc/core/NGnTree.h>\n\nusing json = nlohmann::json;\n\n{\n"),
            std::string::npos)
      << macro;
  EXPECT_NE(macro.find("gSystem->Setenv(\"NDMSPC_MAX_PROCESSES\", \"4\");"), std::string::npos) << macro;
  EXPECT_NE(macro.find("gSystem->Setenv(\"NDMSPC_EXECUTION_MODE\", \"ipc\");"), std::string::npos) << macro;
  EXPECT_NE(macro.find("gSystem->Setenv(\"NDMSPC_LOG_RUN\", \"1\");"), std::string::npos) << macro;
  EXPECT_NE(macro.find("json cfg;"), std::string::npos) << macro;
  EXPECT_NE(macro.find("cfg[\"type\"]"), std::string::npos) << macro;
  EXPECT_NE(macro.find("\"thnsparse\""), std::string::npos) << macro;
  EXPECT_NE(macro.find("cfg[\"axes\"]"), std::string::npos) << macro;
  // Written as the braces nlohmann reads them as, rather than wrapped in `json::array(…)`.
  EXPECT_NE(macro.find("cfg[\"axes\"]        = {\"mass\", \"pt\"};"), std::string::npos) << macro;
  EXPECT_NE(macro.find("cfg[\"objects\"]     = {\"hns\"};"), std::string::npos) << macro;
  EXPECT_NE(macro.find("cfg[\"projections\"]"), std::string::npos) << macro;
  // A projection names its axes (`{"mass"}`), which is what the configuration carries.
  EXPECT_NE(macro.find("= {{\"mass\"}};"), std::string::npos) << macro;
  // A map is written as its pairs, not as a flat list of keys and values: `{"mass", "m"}` would be read
  // back as an array.
  EXPECT_NE(macro.find("cfg[\"titles\"]      = {{\"mass\", \"m\"}};"), std::string::npos) << macro;
  EXPECT_NE(macro.find("cfg[\"outputFile\"]"), std::string::npos) << macro;
  EXPECT_NE(macro.find("/tmp/preview_ngnt.root"), std::string::npos) << macro;
  // And the last line hands the configuration to the importer, with the file being browsed — the same
  // argument the run passes, and never an empty one.
  EXPECT_NE(macro.find(std::string("Ndmspc::NGnTree::Import(cfg, \"") + kFile + "\")"), std::string::npos) << macro;

  // It previews and does nothing: no combination node, no session of its own.
  const json combinations = fTool->Server()->GetCombinations();
  const json nodes = combinations.is_object() ? combinations.value("nodes", json::object()) : json::object();
  EXPECT_TRUE(nodes.empty()) << combinations.dump();

  // A form the macro action does not make one for is refused rather than answered with a guess.
  const auto unknown = fTool->Call("ndmspc/rbrowser/macro", "POST",
                                   json{{"for", "ndmspc/rbrowser/draw"}, {"key", "hns"}});
  EXPECT_FALSE(unknown.reply.value("error", std::string()).empty());

  // The **project** tab previews the same way, and the projection is the one the run would make: the axes
  // it projects, the cuts the table's min/max make, and the drawing the room shows. Both read the table
  // through the same derivation, so a preview cannot project something else than the run.
  EXPECT_EQ(dialog.ws["payload"]["dialog"]["tabs"][0]["macro"], "ndmspc/rbrowser/macro");
  const auto projected = fTool->Call(
      "ndmspc/rbrowser/macro", "POST",
      json{{"for", "ndmspc/rbrowser/project"},
           {"key", "hns"},
           {"drawOpts", "colz"},
           {"axes", json::array({json{{"axis", "mass"}, {"use", true}, {"min", 0}, {"max", 1}, {"rebin", 2}},
                                 json{{"axis", "pt"}, {"use", false}, {"min", 2}, {"max", 4}, {"rebin", 1}}})}});
  ASSERT_EQ(projected.reply.value("result", std::string()), "success") << projected.reply.dump();
  const std::string projection = projected.reply["macro"].get<std::string>();
  EXPECT_NE(projection.find("#include <THnSparse.h>"), std::string::npos) << projection;
  EXPECT_NE(projection.find("#include <ndmspc/core/NUtils.h>"), std::string::npos) << projection;
  EXPECT_LT(projection.find("#include"), projection.find("{")) << projection;
  EXPECT_NE(projection.find("(THnSparse *) file->Get(\"hns\")"), std::string::npos) << projection;
  // Only the second axis is narrowed (2..4 out of its range): the first is left whole, so it is no cut.
  EXPECT_NE(projection.find("SetAxisRanges(sparse, {"), std::string::npos) << projection;
  EXPECT_NE(projection.find("ProjectTHnSparse(sparse, {0}, \"\")"), std::string::npos) << projection;
  EXPECT_NE(projection.find("RebinX(2)"), std::string::npos) << projection;
  EXPECT_NE(projection.find("h->Draw(\"colz\");"), std::string::npos) << projection;
}

/// What the import tab was given is remembered for the object's **axes**, so the same import is not filled
/// in twice — the way the projection dialog remembers its own configuration for the same signature.
TEST_F(ToolBrowser, TheImportRemembersWhatItWasGiven)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  const std::string out = "/tmp/browser_import_remember_ngnt.root";
  std::remove(out.c_str());

  const auto run = fTool->Call(
      "ndmspc/rbrowser/import", "POST",
      json{{"key", "hns"},
           {"axes", json::array({"mass", "pt"})},
           {"axesTitles", json::array({json{{"axis", "mass"}, {"title", "m"}, {"rebin", 4}}})},
           {"projections", json::array({json::array({1, 2})})},
           {"outputFile", out}});
  ASSERT_EQ(run.reply.value("result", std::string()), "success") << run.reply.dump();
  std::remove(out.c_str());

  // Opening the dialog again opens on what was run.
  const auto   again = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns"}});
  const json & props = again.ws["payload"]["dialog"]["tabs"][1]["schema"]["properties"];
  EXPECT_EQ(props["axes"]["default"], json::array({"mass", "pt"}));
  EXPECT_EQ(props["axesTitles"]["default"][0]["title"], "m");
  EXPECT_EQ(props["axesTitles"]["default"][0]["rebin"], 4);
  EXPECT_EQ(props["projections"]["default"], json::array({json::array({1, 2})}));
  EXPECT_EQ(props["outputFile"]["default"], out);

  // And so does another object whose axes are the same (`hns2` shares `hns`' axes).
  const auto   sibling = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hns2"}});
  const json & sib     = sibling.ws["payload"]["dialog"]["tabs"][1]["schema"]["properties"];
  EXPECT_EQ(sib["axes"]["default"], json::array({"mass", "pt"}));
  EXPECT_EQ(sib["outputFile"]["default"], out);
  // The projection tab's own configuration is not what the import remembered: the two are kept apart.
  EXPECT_FALSE(again.ws["payload"]["dialog"]["tabs"][0]["schema"]["properties"]["axes"]["default"][0].is_string());
}

/// A **categorical** axis (one that carries a label per bin) cannot be rebinned: ROOT's `Rebin` wants fixed
/// widths and the importer refuses a binning on a labelled axis, so the import tab's rebin cell is off.
TEST_F(ToolBrowser, ACategoricalAxisOffersNoRebinInTheImportTab)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});
  const auto call = fTool->Call("ndmspc/rbrowser/sparse", "POST", json{{"key", "hcat"}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success") << call.reply.dump();

  const json & rows =
      call.ws["payload"]["dialog"]["tabs"][1]["schema"]["properties"]["axesTitles"]["default"];
  ASSERT_EQ(rows.size(), 1u) << rows.dump();
  EXPECT_EQ(rows[0]["axis"], "type");
  EXPECT_EQ(rows[0]["bins"], 3);
  ASSERT_TRUE(rows[0].contains("disabled")) << "a categorical axis must not offer a rebin";
  EXPECT_EQ(rows[0]["disabled"], json::array({"rebin"}));
}

/// The import is split over the processes the form asks for — and the room's own environment is put back
/// as it was afterwards (the environment is process-wide, and the room runs other things too).
TEST_F(ToolBrowser, TheImportRunsWithTheProcessesTheFormAsksFor)
{
  fTool->Call("ndmspc/browser/open", "POST", json{{"file", kFile}});

  const std::string out = "/tmp/browser_import_processes_ngnt.root";
  std::remove(out.c_str());

  // A setting of the room's own, which this run must leave alone — as must the logger's run-output flag,
  // which is what the run's progress prints under (`NDMSPC_LOG_RUN=1`).
  gSystem->Setenv("NDMSPC_MAX_PROCESSES", "1");
  const bool runOutputWas = Ndmspc::NLogger::GetRunOutput();
  {
    const auto run = fTool->Call(
        "ndmspc/rbrowser/import", "POST",
        json{{"key", "hns"},
             {"axes", json::array({"mass", "pt"})},
             {"projections", json::array({json::array({0}), json::array({1})})},
             {"nProcesses", 2},
             {"outputFile", out}});
    ASSERT_EQ(run.reply.value("result", std::string()), "success") << run.reply.dump();
    EXPECT_FALSE(gSystem->AccessPathName(out.c_str())) << "the import wrote no file";
  }

  // Back to what the room had, not what the run set.
  const char * left = gSystem->Getenv("NDMSPC_MAX_PROCESSES");
  EXPECT_NE(left, nullptr) << "the run left the room's environment unset";
  if (left != nullptr) EXPECT_EQ(std::string(left), "1");
  gSystem->Unsetenv("NDMSPC_MAX_PROCESSES");
  EXPECT_EQ(Ndmspc::NLogger::GetRunOutput(), runOutputWas);

  std::remove(out.c_str());
}

} // namespace
