/**
 * The browser tool, loaded and called the way the server loads and calls it.
 *
 * Covers the browsing loop: `browser/open` publishes the file's tree onto the `browser/browse` step
 * (no pad is filled), `browser/browse` re-publishes it, and the internal `rbrowser/ls` / `rbrowser/draw`
 * — which the tree's nodes drive — expand a folder and draw an object. The internals sit outside the
 * `browser` combination on purpose: the server records a node for every POST of an action in a
 * combination group, and browsing must add no step.
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
#include <TNtuple.h>

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

class ToolBrowser : public ::testing::Test {
  protected:
  static void SetUpTestSuite() { WriteTestFile(); }
  void SetUp() override { fTool = std::make_unique<ToolHarness>(kToolMacro); }

  std::unique_ptr<ToolHarness> fTool;
};

TEST_F(ToolBrowser, RegistersTheBrowseStepAndItsHiddenInternals)
{
  for (const char * key : {"browser/open", "browser/browse", "rbrowser/ls", "rbrowser/draw",
                           "rbrowser/sparse", "rbrowser/project"}) {
    EXPECT_NE(fTool->Handler(key), nullptr) << key << " did not register a handler";
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(key);
    ASSERT_NE(info, nullptr) << key << " registered no metadata";
    EXPECT_TRUE(info->inputSchema.contains("properties")) << key << " has no properties";
  }

  // `open` starts the combination; `browse` hangs under it, and its form is the file tree.
  const Ndmspc::NMcpToolInfo * open = fTool->ToolInfo("browser/open");
  EXPECT_TRUE(open->dependsOn.empty()) << "open is the head of the chain";
  EXPECT_EQ(open->label, "{{ file }}");
  EXPECT_EQ(open->order, 1);
  EXPECT_EQ(open->inputSchema["properties"]["file"].value("default", std::string()),
            "https://root.cern/js/files/hsimple.root");
  // The tool names no pad: the pad view routes what it shows.
  EXPECT_FALSE(open->inputSchema["properties"].contains("pad"));

  const Ndmspc::NMcpToolInfo * browse = fTool->ToolInfo("browser/browse");
  ASSERT_EQ(browse->dependsOn.size(), 1u);
  EXPECT_EQ(browse->dependsOn[0], "browser/open");
  EXPECT_EQ(browse->inputSchema["properties"]["key"].value("format", std::string()), "tree");
  EXPECT_FALSE(browse->inputSchema["properties"].contains("pad"));

  // The internals are a plain, hidden group: they add no step, and neither list shows them.
  for (const char * key : {"rbrowser/ls", "rbrowser/draw", "rbrowser/sparse", "rbrowser/project"}) {
    const Ndmspc::NMcpToolInfo * info = fTool->ToolInfo(key);
    EXPECT_TRUE(info->hidden) << key << " should be hidden";
    EXPECT_TRUE(info->dependsOn.empty()) << key << " should add no step";
  }
}

TEST_F(ToolBrowser, OpenPublishesTheFileTreeOntoTheBrowseStep)
{
  const auto call = fTool->Call("browser/open", "POST", json{{"file", kFile}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success");

  // The tree belongs to the browse step, so `open` fills no pad.
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);

  const json & tree = call.ws["workspace"]["browse"]["properties"]["key"];
  EXPECT_EQ(tree.value("format", std::string()), "tree");
  ASSERT_TRUE(tree["nodes"].is_array());

  const json * hpx = FindNode(tree["nodes"], "hpx;1");
  ASSERT_NE(hpx, nullptr) << "the top-level histogram is missing from the tree";
  EXPECT_EQ((*hpx)["detail"], "TH1D");
  EXPECT_EQ((*hpx)["action"]["path"], "rbrowser/draw");
  EXPECT_EQ((*hpx)["action"]["payload"]["key"], "hpx");
  // The tree's pad must not ride along on the draw action, or the object lands beside the tree.
  EXPECT_FALSE((*hpx)["action"]["payload"].contains("pad"));

  const json * sub = FindNode(tree["nodes"], "sub;1");
  ASSERT_NE(sub, nullptr) << "the folder is missing from the tree";
  EXPECT_EQ((*sub)["expandable"], true);
  EXPECT_EQ((*sub)["action"]["path"], "rbrowser/ls");
}

TEST_F(ToolBrowser, LsSendsTheTreeWithTheFolderExpanded)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const auto call = fTool->Call("rbrowser/ls", "POST", json{{"key", "sub"}});
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
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const auto expanded = fTool->Call("rbrowser/ls", "POST", json{{"key", "ntuple"}});

  const json * nt =
      FindNode(expanded.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "ntuple;1");
  ASSERT_NE(nt, nullptr) << "the tree is missing from the tree";
  EXPECT_EQ((*nt)["action"]["path"], "rbrowser/draw");
  EXPECT_EQ((*nt)["expandable"], true);
  ASSERT_TRUE((*nt).contains("children"));
  EXPECT_GE((*nt)["children"].size(), 3u);

  // A branch draws itself: clicking it projects the branch into a histogram.
  const json & px = (*nt)["children"][0];
  EXPECT_EQ(px["action"]["path"], "rbrowser/draw");
  EXPECT_EQ(px["action"]["payload"]["branch"], "px");

  const auto drawn     = fTool->Call("rbrowser/draw", "POST", px["action"]["payload"]);
  const json drawnPads = ToolHarness::Pads(drawn);
  ASSERT_EQ(drawnPads.size(), 1u);
  EXPECT_EQ(drawnPads[0].value("kind", std::string()), "jsroot");
  // No pad is named: the pad view decides where it lands.
  EXPECT_TRUE(drawnPads[0].value("pad", std::string()).empty());
  EXPECT_EQ(drawnPads[0]["value"]["fName"], "px");
}

TEST_F(ToolBrowser, DrawTurnsAKeyIntoAJsrootEnvelope)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const auto call = fTool->Call("rbrowser/draw", "POST", json{{"key", "hpx"}});
  ASSERT_EQ(call.reply.value("result", std::string()), "success");

  const json pads = ToolHarness::Pads(call);
  ASSERT_EQ(pads.size(), 1u);
  EXPECT_EQ(pads[0].value("kind", std::string()), "jsroot");
  EXPECT_TRUE(pads[0].value("pad", std::string()).empty());
  EXPECT_EQ(pads[0]["value"]["fName"], "hpx");
}

TEST_F(ToolBrowser, ANodeActionNamesNoPadSoTheViewRoutesIt)
{
  const auto   open = fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const json * hpx =
      FindNode(open.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "hpx;1");
  ASSERT_NE(hpx, nullptr);
  const json payload = (*hpx)["action"]["payload"];
  EXPECT_FALSE(payload.contains("pad"));

  // Exactly what a click in the browse step's tree would make: the node's own payload.
  const auto draw      = fTool->Call("rbrowser/draw", "POST", payload);
  const json drawnPads = ToolHarness::Pads(draw);
  ASSERT_EQ(drawnPads.size(), 1u);
  // The tool names no pad, so the pad view decides (fixed pad, or the rotating ones).
  EXPECT_TRUE(drawnPads[0].value("pad", std::string()).empty());
}

TEST_F(ToolBrowser, DrawingWithoutAnOpenFileIsRefused)
{
  const auto call = fTool->Call("rbrowser/draw", "POST", json{{"key", "hpx"}});
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);
  EXPECT_FALSE(call.reply.value("error", std::string()).empty());
}

TEST_F(ToolBrowser, DeleteClosesTheFile)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  ASSERT_NE(fTool->Objects().find("browserFile"), fTool->Objects().end());

  const auto call = fTool->Call("browser/open", "DELETE");
  EXPECT_EQ(call.reply.value("result", std::string()), "success");
  EXPECT_EQ(fTool->Objects().find("browserFile"), fTool->Objects().end());
}

TEST_F(ToolBrowser, ASparseNodeOpensAProjectionDialog)
{
  const auto open = fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const json * hns =
      FindNode(open.ws["workspace"]["browse"]["properties"]["key"]["nodes"], "hns;1");
  ASSERT_NE(hns, nullptr) << "the THnSparse is missing from the tree";
  // jsroot cannot draw a sparse, so the node is a leaf whose click opens the dialog rather than drawing.
  EXPECT_FALSE((*hns).value("expandable", false));
  EXPECT_EQ((*hns)["action"]["path"], "rbrowser/sparse");
  EXPECT_EQ((*hns)["action"]["payload"]["key"], "hns");

  const auto call = fTool->Call("rbrowser/sparse", "POST", (*hns)["action"]["payload"]);
  ASSERT_EQ(call.reply.value("result", std::string()), "success");
  // The dialog is the message: nothing is drawn until the form is submitted.
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);

  const json & dialog = call.ws["payload"]["dialog"];
  EXPECT_EQ(dialog.value("title", std::string()), "Project hns");
  EXPECT_EQ(dialog["action"]["path"], "rbrowser/project");
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

  // The table has one row per axis and a checkbox column (`use`); the first axis is ticked by default,
  // so the form's first run is a 1D projection.
  ASSERT_TRUE(props["axes"]["columns"].is_array());
  EXPECT_EQ(props["axes"]["columns"][0].value("key", std::string()), "use");
  EXPECT_EQ(props["axes"]["columns"][1].value("key", std::string()), "axis");
  // The label is TLatex, so the column says how to render it.
  EXPECT_EQ(props["axes"]["columns"][1].value("format", std::string()), "rootlatex");

  // One row per axis, labelled "index, name [title]"; the first axis is ticked by default, so the
  // form's first run is a 1D projection.
  ASSERT_TRUE(props["axes"]["default"].is_array());
  ASSERT_EQ(props["axes"]["default"].size(), 3u);
  EXPECT_EQ(props["axes"]["default"][0].value("axis", std::string()), "0, mass [invariant mass]");
  EXPECT_EQ(props["axes"]["default"][1].value("axis", std::string()), "1, pt [p_{T}]");
  EXPECT_EQ(props["axes"]["default"][0].value("use", false), true);
  EXPECT_EQ(props["axes"]["default"][1].value("use", true), false);
}

TEST_F(ToolBrowser, ProjectingASparseDrawsTheChosenAxes)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // 1D: one axis ticked in the table.
  const auto one =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  ASSERT_EQ(one.reply.value("result", std::string()), "success");
  const json onePads = ToolHarness::Pads(one);
  ASSERT_EQ(onePads.size(), 1u);
  EXPECT_EQ(onePads[0].value("kind", std::string()), "jsroot");
  // No name given: the projection is named after the default ("projection").
  EXPECT_EQ(onePads[0]["value"]["fName"], "projection");
  EXPECT_EQ(onePads[0]["value"].value("_typename", std::string()).rfind("TH1", 0), 0u);

  // 2D: two axes.
  const auto two =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0, 1})}});
  const json twoPads = ToolHarness::Pads(two);
  ASSERT_EQ(twoPads.size(), 1u);
  EXPECT_EQ(twoPads[0]["value"].value("_typename", std::string()).rfind("TH2", 0), 0u);

  // 3D: three axes.
  const auto three =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0, 1, 2})}});
  const json threePads = ToolHarness::Pads(three);
  ASSERT_EQ(threePads.size(), 1u);
  EXPECT_EQ(threePads[0]["value"].value("_typename", std::string()).rfind("TH3", 0), 0u);
}

TEST_F(ToolBrowser, ProjectingWithoutAnAxisIsRefused)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const auto call =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({})}});
  EXPECT_EQ(ToolHarness::Pads(call).size(), 0u);
  EXPECT_FALSE(call.reply.value("error", std::string()).empty());
}

TEST_F(ToolBrowser, ASparseRangeNarrowsTheProjection)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const json axes = json::array({
      {{"use", true}, {"min", 0.2}, {"max", 1.0}},
      {{"use", false}},
      {{"use", false}},
  });
  const auto call = fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", axes}});
  const json pads = ToolHarness::Pads(call);
  ASSERT_EQ(pads.size(), 1u);
  // Axis 0 has 20 bins over [0,2]; [0.2,1.0] is a fraction of it.
  ASSERT_TRUE(pads[0]["value"].contains("fXaxis"));
  EXPECT_LT(pads[0]["value"]["fXaxis"].value("fNbins", 0), 20);
}

TEST_F(ToolBrowser, ASparseRangeOnANonProjectedAxisNarrowsTheProjection)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  const auto full =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});

  // Axis 1 (pt) is not projected, but a range on it must still carve the sample the mass projection is
  // built from: point b has pt 3, so a [0,2] cut leaves only point a.
  const json axes = json::array({
      {{"use", true}},
      {{"use", false}, {"min", 0.0}, {"max", 2.0}},
      {{"use", false}},
  });
  const auto cut = fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", axes}});

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
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const auto plain =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  const json axes =
      json::array({{{"use", true}, {"rebin", 2}}, {{"use", false}}, {{"use", false}}});
  const auto rebinned =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", axes}});
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
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // Project hns on axis 1 with a range and "same canvas", which keeps that configuration.
  const json axes = json::array({
      {{"use", false}},
      {{"use", true}, {"min", 1.0}, {"max", 3.0}, {"rebin", 2}},
      {{"use", false}},
  });
  const auto first = fTool->Call("rbrowser/project", "POST",
                                 json{{"key", "hns"}, {"axes", axes}, {"same", true}});
  ASSERT_EQ(first.reply.value("result", std::string()), "success");
  // The default name is the canvas: `projection`.
  const json firstPads = ToolHarness::Pads(first);
  ASSERT_EQ(firstPads.size(), 1u);
  EXPECT_EQ(firstPads[0].value("label", std::string()), "projection");

  // Reopening the dialog opens on the saved configuration ...
  const auto   again = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
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
  const auto   other     = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns2"}});
  const json & otherRows = other.ws["payload"]["dialog"]["schema"]["properties"]["axes"]["default"];
  ASSERT_EQ(otherRows.size(), 3u);
  EXPECT_EQ(otherRows[1].value("use", false), true);
  EXPECT_DOUBLE_EQ(otherRows[1].value("max", 0.0), 3.0);
}

TEST_F(ToolBrowser, ASparseSameCanvasOverlaysTheProjections)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // Projections share a canvas by name — `cmp` is that canvas here.
  const auto one =
      fTool->Call("rbrowser/project", "POST",
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
      fTool->Call("rbrowser/project", "POST",
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
      fTool->Call("rbrowser/project", "POST",
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
      fTool->Call("rbrowser/project", "POST",
                  json{{"key", "hns"}, {"axes", ranged}, {"same", true}, {"name", "cmp"}});
  const json thirdPads = ToolHarness::Pads(third);
  ASSERT_EQ(thirdPads.size(), 1u);
  EXPECT_EQ(CountOf(thirdPads[0]["value"].dump(), "TH1D"), 3);
}

TEST_F(ToolBrowser, ASparseNamedCanvasesAreKeptApart)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // Two canvases, by name; each keeps its own curves.
  fTool->Call("rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "A"}});
  fTool->Call("rbrowser/project", "POST",
              json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "B"}});

  // A grows ...
  const auto a = fTool->Call(
      "rbrowser/project", "POST",
      json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "A"}});
  const json aPads = ToolHarness::Pads(a);
  ASSERT_EQ(aPads.size(), 1u);
  EXPECT_EQ(aPads[0].value("label", std::string()), "A");
  EXPECT_EQ(CountOf(aPads[0]["value"].dump(), "TH1D"), 2);

  // ... and B is untouched by it.
  const auto b = fTool->Call(
      "rbrowser/project", "POST",
      json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}, {"name", "B"}});
  const json bPads = ToolHarness::Pads(b);
  ASSERT_EQ(bPads.size(), 1u);
  EXPECT_EQ(bPads[0].value("label", std::string()), "B");
  EXPECT_EQ(CountOf(bPads[0]["value"].dump(), "TH1D"), 1);
}

TEST_F(ToolBrowser, ASparseSameCanvasIncludesEarlierProjections)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // One projection drawn on its own (Same canvas off) is still remembered for the file ...
  const auto own =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  const json ownPads = ToolHarness::Pads(own);
  ASSERT_EQ(ownPads.size(), 1u);
  EXPECT_EQ(ownPads[0].value("label", std::string()), "projection");

  // ... so turning Same canvas on shows it together with the next one.
  const auto both = fTool->Call("rbrowser/project", "POST",
                                json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}});
  const json pads = ToolHarness::Pads(both);
  ASSERT_EQ(pads.size(), 1u);
  EXPECT_EQ(pads[0].value("label", std::string()), "projection");
  EXPECT_EQ(CountOf(pads[0]["value"].dump(), "TH1D"), 2);
}

TEST_F(ToolBrowser, ASparseSameCanvasOffStartsANewCanvas)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  fTool->Call("rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}});

  // Same canvas off draws the projection as its own object — and starts a new canvas, forgetting the
  // one before it.
  const auto own =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns2"}, {"axes", AxesTable({0})}});
  const json ownPads = ToolHarness::Pads(own);
  ASSERT_EQ(ownPads.size(), 1u);
  EXPECT_EQ(ownPads[0].value("label", std::string()), "projection");

  // The canvas in hand now holds only that one: overlaying the same object again does not bring back
  // the projection the new canvas forgot.
  const auto after = fTool->Call("rbrowser/project", "POST",
                                 json{{"key", "hns2"}, {"axes", AxesTable({0})}, {"same", true}});
  const json afterPads = ToolHarness::Pads(after);
  ASSERT_EQ(afterPads.size(), 1u);
  EXPECT_EQ(CountOf(afterPads[0]["value"].dump(), "TH1D"), 1);
}

TEST_F(ToolBrowser, ASparseSameChoiceIsRemembered)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // Off to begin with — a projection drawn as its own object starts the canvas.
  const auto first = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(first.ws["payload"]["dialog"]["schema"]["properties"]["same"].value("default", true), false);

  // Ticked, it stays ticked for the next dialog.
  fTool->Call("rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", true}});
  const auto ticked = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(ticked.ws["payload"]["dialog"]["schema"]["properties"]["same"].value("default", false), true);

  // Unticked, it stays unticked — and starts another canvas.
  fTool->Call("rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"same", false}});
  const auto unticked = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(unticked.ws["payload"]["dialog"]["schema"]["properties"]["same"].value("default", true), false);
}

TEST_F(ToolBrowser, ASparseNameIsRemembered)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // "projection" to begin with ...
  const auto first = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(first.ws["payload"]["dialog"]["schema"]["properties"]["name"].value("default", std::string()),
            "projection");

  // ... and the last name used comes back with the rest of the configuration.
  fTool->Call("rbrowser/project", "POST",
              json{{"key", "hns"}, {"axes", AxesTable({0})}, {"name", "MyCanvas"}});
  const auto again = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(again.ws["payload"]["dialog"]["schema"]["properties"]["name"].value("default", std::string()),
            "MyCanvas");
}

TEST_F(ToolBrowser, ASparseIsRereadAfterEachProjection)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // Every request frees the sparse it read, so the next one reads it again: two projections and a
  // dialog all work on the same object.
  const auto a =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  EXPECT_EQ(a.reply.value("result", std::string()), "success");
  const auto b =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({1})}});
  EXPECT_EQ(b.reply.value("result", std::string()), "success");
  const auto dialog = fTool->Call("rbrowser/sparse", "POST", json{{"key", "hns"}});
  EXPECT_EQ(dialog.reply.value("result", std::string()), "success");
  EXPECT_TRUE(dialog.ws["payload"]["dialog"].contains("schema"));
}

TEST_F(ToolBrowser, ASparseOverlayIsCapped)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});

  // Twelve distinct projections onto one name: the canvas keeps the last ten and drops the oldest.
  json last;
  for (int i = 0; i < 12; i++) {
    const json axes = json::array({
        {{"use", true}, {"min", 0.0}, {"max", 0.05 * (i + 1)}},
        {{"use", false}},
        {{"use", false}},
    });
    last = ToolHarness::Pads(fTool->Call(
        "rbrowser/project", "POST",
        json{{"key", "hns"}, {"axes", axes}, {"same", true}, {"name", "cap"}}))[0];
  }
  EXPECT_EQ(last["value"].value("_typename", std::string()), "TCanvas");
  EXPECT_EQ(CountOf(last["value"].dump(), "TH1D"), 10);
}

TEST_F(ToolBrowser, ASparseProjectionWithoutSameCanvasKeepsItsOwnTab)
{
  fTool->Call("browser/open", "POST", json{{"file", kFile}});
  const auto call =
      fTool->Call("rbrowser/project", "POST", json{{"key", "hns"}, {"axes", AxesTable({0})}});
  const json pads = ToolHarness::Pads(call);
  ASSERT_EQ(pads.size(), 1u);
  EXPECT_EQ(pads[0].value("label", std::string()), "projection");
}

} // namespace
