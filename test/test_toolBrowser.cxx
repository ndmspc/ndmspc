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

#include <memory>
#include <string>

#include <TDirectory.h>
#include <TFile.h>
#include <TH1D.h>
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

class ToolBrowser : public ::testing::Test {
  protected:
  static void SetUpTestSuite() { WriteTestFile(); }
  void SetUp() override { fTool = std::make_unique<ToolHarness>(kToolMacro); }

  std::unique_ptr<ToolHarness> fTool;
};

TEST_F(ToolBrowser, RegistersTheBrowseStepAndItsHiddenInternals)
{
  for (const char * key : {"browser/open", "browser/browse", "rbrowser/ls", "rbrowser/draw"}) {
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
  for (const char * key : {"rbrowser/ls", "rbrowser/draw"}) {
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

} // namespace
