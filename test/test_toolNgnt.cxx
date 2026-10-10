/**
 * The ngnt tool, loaded and called the way the server loads and calls it.
 *
 * What this covers today is the tool's **registration** and its handler surface: the macro loads, its
 * entry function registers the chain with its metadata, and the handlers the server would route to
 * exist. What it deliberately does not cover is the chain itself — `open` runs (the file opens and its
 * binning is imported) but a hand-called `reshape` stops at "NGnTree is not opened", because the chain
 * leans on the server's per-request plumbing (the node materialization and the workspace state) that a
 * direct handler call does not go through. Walking the chain needs a harness that drives the *server*
 * with a websocket client attached, which is where this should grow next.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>

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

/// A tool under test: its macro, loaded, with the handlers it registered.
class ToolNgnt : public ::testing::Test {
  protected:
  void SetUp() override { fTool = std::make_unique<ToolHarness>(kToolMacro); }

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

} // namespace
