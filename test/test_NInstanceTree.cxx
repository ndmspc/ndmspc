#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NInstanceTree.h"

using Ndmspc::NInstanceTree;

TEST(NInstanceTreeTest, CreateLinksParentAndPath)
{
  json         store;
  NInstanceTree tree(store);

  const std::string a = tree.Create("ngnt/open", {{"file", "a.root"}}, "", "");
  const std::string b = tree.Create("ngnt/reshape", {{"binningName", "x"}}, a, "");
  const std::string c = tree.Create("ngnt/map", {{"mappingPad", "pad1"}}, b, "");

  EXPECT_EQ(a, "i1");
  EXPECT_EQ(b, "i2");
  EXPECT_EQ(c, "i3");
  EXPECT_EQ(tree.Parent(b), a);
  EXPECT_EQ(tree.Action(c), "ngnt/map");
  EXPECT_EQ(tree.Path(c), (std::vector<std::string>{a, b, c}));
  ASSERT_EQ(tree.Roots().size(), 1u);
  EXPECT_EQ(tree.Roots()[0], a);
  EXPECT_EQ(tree.Children(a), (std::vector<std::string>{b}));
  // The label defaults to the first non-empty string argument.
  EXPECT_EQ(tree.Get(a)["label"].get<std::string>(), "a.root");

  tree.SetActive(tree.Path(c));
  EXPECT_EQ(tree.Active(), (std::vector<std::string>{a, b, c}));
}

TEST(NInstanceTreeTest, RemoveSubtreeDropsChildrenAndCallsBackDeepestFirst)
{
  json         store;
  NInstanceTree tree(store);
  const std::string a = tree.Create("ngnt/open", json::object(), "", "");
  const std::string b = tree.Create("ngnt/reshape", json::object(), a, "");
  const std::string c = tree.Create("ngnt/map", json::object(), b, "");
  tree.SetActive(tree.Path(c));

  std::vector<std::string> removed;
  EXPECT_TRUE(tree.RemoveSubtree(b, [&](const std::string & id, const std::string &) { removed.push_back(id); }));

  EXPECT_EQ(removed, (std::vector<std::string>{c, b}));
  EXPECT_FALSE(tree.Has(b));
  EXPECT_FALSE(tree.Has(c));
  EXPECT_TRUE(tree.Has(a));
  EXPECT_TRUE(tree.Children(a).empty());
  // The active path pointed into the removed subtree, so it collapses to the surviving ancestor.
  EXPECT_EQ(tree.Active(), (std::vector<std::string>{a}));
}

TEST(NInstanceTreeTest, ToTreeIsNestedAndSnapshotRoundTrips)
{
  json         store;
  NInstanceTree tree(store);
  const std::string a = tree.Create("ngnt/open", json::object(), "", "");
  const std::string b = tree.Create("ngnt/reshape", json::object(), a, "");
  const std::string c = tree.Create("ngnt/reshape", json::object(), a, "");
  tree.SetActive({a, c});

  const json t = tree.ToTree();
  ASSERT_EQ(t["roots"].size(), 1u);
  EXPECT_EQ(t["roots"][0]["id"], a);
  ASSERT_EQ(t["roots"][0]["children"].size(), 2u);
  EXPECT_EQ(t["roots"][0]["children"][0]["id"], b);
  EXPECT_EQ(t["roots"][0]["children"][1]["id"], c);
  EXPECT_EQ(t["active"], json::array({a, c}));

  json          restored;
  NInstanceTree other(restored);
  other.Restore(tree.Snapshot());
  EXPECT_EQ(other.Active(), (std::vector<std::string>{a, c}));
  EXPECT_EQ(other.Path(c), (std::vector<std::string>{a, c}));
  EXPECT_EQ(other.Children(a), (std::vector<std::string>{b, c}));
}

TEST(NInstanceTreeTest, IsNodeActionFollowsTheDeclaredDependencies)
{
  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ngnt/open"]             = {};
  tools["ngnt/reshape"]          = {.dependsOn = {"ngnt/open"}};
  tools["health"]                = {};
  Ndmspc::gNdmspcMcpTools        = &tools;

  // A group does not become a combination tree until one of its actions declares a dependency.
  EXPECT_TRUE(NInstanceTree::IsNodeAction("ngnt/open"));
  EXPECT_TRUE(NInstanceTree::IsNodeAction("ngnt/reshape"));
  EXPECT_FALSE(NInstanceTree::IsNodeAction("health"));
  EXPECT_EQ(NInstanceTree::ParentActionFor("ngnt/reshape"), "ngnt/open");
  EXPECT_EQ(NInstanceTree::ParentActionFor("ngnt/open"), "");

  Ndmspc::gNdmspcMcpTools = previous;
}

TEST(NInstanceTreeTest, LabelTemplateRendersFromTheArguments)
{
  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ngnt/open"]             = {.label = "{{ file }}"};
  tools["ngnt/reshape"]          = {.label = "{{ binningName }} ({{ levels }})"};
  tools["ngnt/map"]              = {.label = "{{ mappingPad }}"};
  tools["ngnt/point"]            = {}; // no template: the label is guessed from the arguments
  Ndmspc::gNdmspcMcpTools        = &tools;

  json          store;
  NInstanceTree tree(store);

  const std::string a = tree.Create("ngnt/open", {{"file", "a.root"}}, "", "");
  EXPECT_EQ(tree.Get(a)["label"].get<std::string>(), "a.root");

  // An array argument reads as its own JSON, and the template's spacing survives.
  const std::string b = tree.Create(
      "ngnt/reshape", {{"binningName", "b0"}, {"levels", json::parse("[[0,1,2],[3,4]]")}}, a, "");
  EXPECT_EQ(tree.Get(b)["label"].get<std::string>(), "b0 ([[0,1,2],[3,4]])");

  // A missing argument is dropped, and the leftover spacing tidied; with nothing left the label
  // falls back rather than reading "()".
  const std::string c = tree.Create("ngnt/map", json::object(), b, "");
  EXPECT_EQ(tree.Get(c)["label"].get<std::string>(), "map");
  const std::string d = tree.Create("ngnt/map", {{"mappingPad", "pad2"}}, b, "");
  EXPECT_EQ(tree.Get(d)["label"].get<std::string>(), "pad2");

  // An action that declares no template keeps the guessed label.
  const std::string e = tree.Create("ngnt/point", {{"contentPad", "pad9"}}, c, "");
  EXPECT_EQ(tree.Get(e)["label"].get<std::string>(), "pad9");

  Ndmspc::gNdmspcMcpTools = previous;
}
