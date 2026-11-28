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

  const std::string a = tree.Create("ndmspc/ngnt/open", {{"file", "a.root"}}, "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, a, "");
  const std::string c = tree.Create("ndmspc/ngnt/map", {{"mappingPad", "pad1"}}, b, "");

  EXPECT_EQ(a, "i1");
  EXPECT_EQ(b, "i2");
  EXPECT_EQ(c, "i3");
  EXPECT_EQ(tree.Parent(b), a);
  EXPECT_EQ(tree.Action(c), "ndmspc/ngnt/map");
  EXPECT_EQ(tree.Path(c), (std::vector<std::string>{a, b, c}));
  ASSERT_EQ(tree.Roots().size(), 1u);
  EXPECT_EQ(tree.Roots()[0], a);
  EXPECT_EQ(tree.Children(a), (std::vector<std::string>{b}));
  // The label defaults to the first non-empty string argument.
  EXPECT_EQ(tree.Get(a)["label"].get<std::string>(), "a.root");

  tree.SetActive(tree.Path(c));
  EXPECT_EQ(tree.Active(), (std::vector<std::string>{a, b, c}));
}

TEST(NInstanceTreeTest, KeepsEachGroupsLiveChainApart)
{
  json          store;
  NInstanceTree tree(store);

  const std::string a = tree.Create("ndmspc/ngnt/open", {{"file", "analysis.root"}}, "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, a, "");
  const std::string c = tree.Create("ndmspc/browser/open", {{"file", "browse.root"}}, "", "");

  tree.SetActive("ndmspc/ngnt", {a, b});
  tree.SetActive("ndmspc/browser", {c});

  // Each group has its own live chain: the browser's file stays open while the analysis group works.
  EXPECT_EQ(tree.Active("ndmspc/ngnt"), (std::vector<std::string>{a, b}));
  EXPECT_EQ(tree.Active("ndmspc/browser"), (std::vector<std::string>{c}));

  // Moving one group's chain leaves the other's where it was.
  tree.SetActive("ndmspc/ngnt", {});
  EXPECT_TRUE(tree.Active("ndmspc/ngnt").empty());
  EXPECT_EQ(tree.Active("ndmspc/browser"), (std::vector<std::string>{c}));

  // Both live chains survive a snapshot/restore.
  const json snapshot = tree.Snapshot();
  json       store2;
  NInstanceTree restored(store2);
  restored.Restore(snapshot);
  restored.SetActive("ndmspc/ngnt", {a, b});
  EXPECT_EQ(restored.Active("ndmspc/browser"), (std::vector<std::string>{c}));
}

TEST(NInstanceTreeTest, RemoveSubtreeDropsChildrenAndCallsBackDeepestFirst)
{
  json         store;
  NInstanceTree tree(store);
  const std::string a = tree.Create("ndmspc/ngnt/open", json::object(), "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", json::object(), a, "");
  const std::string c = tree.Create("ndmspc/ngnt/map", json::object(), b, "");
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

TEST(NInstanceTreeTest, CreateReusesAnIdenticalNode)
{
  json         store;
  NInstanceTree tree(store);

  const std::string a = tree.Create("ndmspc/ngnt/open", {{"file", "a.root"}}, "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, a, "");

  // Running the same step again is the same instance: the tree keeps one path, and the caller is told
  // which node it is, so a re-run refreshes that node rather than growing a twin beside it.
  EXPECT_EQ(tree.Create("ndmspc/ngnt/open", {{"file", "a.root"}}, "", ""), a);
  EXPECT_EQ(tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, a, ""), b);
  ASSERT_EQ(tree.Roots().size(), 1u);
  EXPECT_EQ(tree.Children(a), (std::vector<std::string>{b}));

  // The request's key order is not part of the identity.
  const std::string ordered = tree.Create(
      "ndmspc/ngnt/reshape", {{"levels", json::parse("[1,2]")}, {"binningName", "z"}}, a, "");
  EXPECT_EQ(tree.Create("ndmspc/ngnt/reshape", {{"binningName", "z"}, {"levels", json::parse("[1,2]")}}, a, ""),
            ordered);

  // A different parameter is a different instance, and so is the same action under another parent.
  const std::string other = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "y"}}, a, "");
  EXPECT_NE(other, b);
  EXPECT_EQ(tree.Children(a), (std::vector<std::string>{b, ordered, other}));
  const std::string deep = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, other, "");
  EXPECT_NE(deep, b);
  EXPECT_EQ(tree.Children(other), (std::vector<std::string>{deep}));

  // An explicitly given label is part of the identity: the same step, named differently, is a second
  // instance — but a call that passes no label does not constrain the match.
  const std::string one = tree.Create("ndmspc/ngnt/map", {{"mappingPad", "pad1"}}, b, "one");
  EXPECT_NE(tree.Create("ndmspc/ngnt/map", {{"mappingPad", "pad1"}}, b, "two"), one);
  EXPECT_EQ(tree.Create("ndmspc/ngnt/map", {{"mappingPad", "pad1"}}, b, "one"), one);
  EXPECT_EQ(tree.Create("ndmspc/ngnt/map", {{"mappingPad", "pad1"}}, b, ""), one);
}

TEST(NInstanceTreeTest, ToTreeIsNestedAndSnapshotRoundTrips)
{
  json         store;
  NInstanceTree tree(store);
  const std::string a = tree.Create("ndmspc/ngnt/open", json::object(), "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, a, "");
  const std::string c = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "y"}}, a, "");
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

TEST(NInstanceTreeTest, SnapshotIsKeyedByGroup)
{
  json         store;
  NInstanceTree tree(store);
  const std::string a = tree.Create("ndmspc/ngnt/open", json::object(), "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", {{"binningName", "x"}}, a, "");
  // A chain that leaves its group: this one is stored under `schema`, its own group, but it hangs
  // under an `ngnt` node.
  const std::string s = tree.Create("schema/start", json::object(), b, "");
  const std::string p = tree.Create("schema/probe", json::object(), s, "");
  tree.SetActive({a, b, s, p});

  const json snapshot = tree.Snapshot();
  EXPECT_EQ(snapshot["v"], 3);
  ASSERT_EQ(snapshot["groups"].size(), 2u);
  // Each node sits under the group of its own action.
  EXPECT_TRUE(snapshot["groups"]["ndmspc/ngnt"]["nodes"].contains(a));
  EXPECT_TRUE(snapshot["groups"]["ndmspc/ngnt"]["nodes"].contains(b));
  EXPECT_TRUE(snapshot["groups"]["schema"]["nodes"].contains(s));
  EXPECT_TRUE(snapshot["groups"]["schema"]["nodes"].contains(p));
  // A group's roots are its entry points: `b` is the ngnt root; `s` starts the schema group because
  // its parent belongs to another group - which is what keeps the schema group whole on its own.
  EXPECT_EQ(snapshot["groups"]["ndmspc/ngnt"]["roots"], json::array({a}));
  EXPECT_EQ(snapshot["groups"]["schema"]["roots"], json::array({s}));
  EXPECT_TRUE(NInstanceTree::HasNodes(snapshot));

  // The whole snapshot still restores the whole tree, chains across groups included.
  json          restored;
  NInstanceTree other(restored);
  other.Restore(snapshot);
  EXPECT_EQ(other.Path(p), (std::vector<std::string>{a, b, s, p}));
  EXPECT_EQ(other.Active(), (std::vector<std::string>{a, b, s, p}));
  EXPECT_EQ(other.Children(b), (std::vector<std::string>{s}));
}

TEST(NInstanceTreeTest, VersionTwoSnapshotStillRestoresAndBecomesVersionThree)
{
  json store;
  // A snapshot written before the tree was keyed by group: one flat map, no `groups`.
  store["v"]      = 2;
  store["next"]   = 3;
  store["active"] = json::array({"i1", "i2"});
  store["nodes"]  = json::object({{"i1", {{"action", "ndmspc/ngnt/open"}, {"params", json::object()}, {"label", "a.root"}, {"children", json::array()}}},
                                  {"i2", {{"action", "ndmspc/ngnt/reshape"},
                                          {"parent", "i1"},
                                          {"params", json::object()},
                                          {"label", "x"},
                                          {"children", json::array()}}}});

  json          restored;
  NInstanceTree tree(restored);
  tree.Restore(store);
  EXPECT_EQ(tree.Path("i2"), (std::vector<std::string>{"i1", "i2"}));
  EXPECT_EQ(tree.Active(), (std::vector<std::string>{"i1", "i2"}));

  // Read into the new shape it regroups, and the next snapshot is v3 with the group as the key.
  const json next = tree.Snapshot();
  EXPECT_EQ(next["v"], 3);
  ASSERT_EQ(next["groups"].size(), 1u);
  EXPECT_EQ(next["groups"]["ndmspc/ngnt"]["roots"], json::array({"i1"}));
}

TEST(NInstanceTreeTest, OneGroupSnapshotsAndRestoresAlone)
{
  json         store;
  NInstanceTree tree(store);
  const std::string a = tree.Create("ndmspc/ngnt/open", json::object(), "", "");
  const std::string b = tree.Create("ndmspc/ngnt/reshape", json::object(), a, "");
  const std::string s = tree.Create("schema/start", json::object(), b, "");
  tree.SetActive({a, b});

  // The group's own nodes *and* what hangs below them, so the chain is not cut in half.
  const json only = tree.Snapshot("ndmspc/ngnt");
  EXPECT_EQ(only["v"], 3);
  ASSERT_TRUE(only["groups"].contains("ndmspc/ngnt"));
  ASSERT_TRUE(only["groups"].contains("schema"));
  EXPECT_TRUE(only["groups"]["schema"]["nodes"].contains(s));

  // Restoring it into a tree that holds other work leaves that work alone, and replaces the group.
  // Node ids are per tree (`i1`, `i2`, ...), so the incoming nodes are re-keyed rather than a node
  // that merely shares a number being overwritten.
  json          targetStore;
  NInstanceTree target(targetStore);
  const std::string x = target.Create("schema/start", json::object(), "", "");
  const std::string y = target.Create("ndmspc/ngnt/open", {{"file", "old.root"}}, "", "");
  target.Restore(only, "ndmspc/ngnt");

  EXPECT_TRUE(target.Has(x));  // the other group is untouched
  EXPECT_FALSE(target.Has(y)); // the group's old nodes are gone

  ASSERT_EQ(target.Roots().size(), 2u);
  EXPECT_EQ(target.Action(target.Roots()[0]), "schema/start");
  const std::string root = target.Roots()[1];
  EXPECT_NE(root, a); // re-keyed rather than merged in by number
  EXPECT_EQ(target.Action(root), "ndmspc/ngnt/open");
  ASSERT_EQ(target.Children(root).size(), 1u);
  const std::string reshaped = target.Children(root)[0];
  EXPECT_EQ(target.Action(reshaped), "ndmspc/ngnt/reshape");
  // The chain below the group came along, and still hangs below it.
  ASSERT_EQ(target.Children(reshaped).size(), 1u);
  EXPECT_EQ(target.Action(target.Children(reshaped)[0]), "schema/start");
}

TEST(NInstanceTreeTest, HasNodesReadsBothVersions)
{
  EXPECT_FALSE(NInstanceTree::HasNodes(json::object()));
  EXPECT_FALSE(NInstanceTree::HasNodes(json{{"v", 3}, {"groups", json::object()}}));
  EXPECT_FALSE(NInstanceTree::HasNodes(json{{"v", 2}, {"nodes", json::object()}}));
  EXPECT_TRUE(NInstanceTree::HasNodes(json{{"v", 2}, {"nodes", json::object({{"i1", json::object()}})}}));
  EXPECT_TRUE(NInstanceTree::HasNodes(
      json{{"v", 3}, {"groups", json::object({{"ndmspc/ngnt", json{{"nodes", json::object({{"i1", json::object()}})}}}})}}));
}

TEST(NInstanceTreeTest, IsNodeActionFollowsTheDeclaredDependencies)
{
  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]             = {};
  tools["ndmspc/ngnt/reshape"]          = {.dependsOn = {"ndmspc/ngnt/open"}};
  tools["health"]                = {};
  Ndmspc::gNdmspcMcpTools        = &tools;

  // A group does not become a combination tree until one of its actions declares a dependency.
  EXPECT_TRUE(NInstanceTree::IsNodeAction("ndmspc/ngnt/open"));
  EXPECT_TRUE(NInstanceTree::IsNodeAction("ndmspc/ngnt/reshape"));
  EXPECT_FALSE(NInstanceTree::IsNodeAction("health"));
  EXPECT_EQ(NInstanceTree::ParentActionFor("ndmspc/ngnt/reshape"), "ndmspc/ngnt/open");
  EXPECT_EQ(NInstanceTree::ParentActionFor("ndmspc/ngnt/open"), "");

  Ndmspc::gNdmspcMcpTools = previous;
}

TEST(NInstanceTreeTest, LabelTemplateRendersFromTheArguments)
{
  Ndmspc::NMcpToolMap   tools;
  Ndmspc::NMcpToolMap * previous = Ndmspc::gNdmspcMcpTools;
  tools["ndmspc/ngnt/open"]             = {.label = "{{ file }}"};
  tools["ndmspc/ngnt/reshape"]          = {.label = "{{ binningName }} ({{ levels }})"};
  tools["ndmspc/ngnt/map"]              = {.label = "{{ mappingPad }}"};
  tools["ndmspc/ngnt/point"]            = {}; // no template: the label is guessed from the arguments
  Ndmspc::gNdmspcMcpTools        = &tools;

  json          store;
  NInstanceTree tree(store);

  const std::string a = tree.Create("ndmspc/ngnt/open", {{"file", "a.root"}}, "", "");
  EXPECT_EQ(tree.Get(a)["label"].get<std::string>(), "a.root");

  // An array argument reads as its own JSON, and the template's spacing survives.
  const std::string b = tree.Create(
      "ndmspc/ngnt/reshape", {{"binningName", "b0"}, {"levels", json::parse("[[0,1,2],[3,4]]")}}, a, "");
  EXPECT_EQ(tree.Get(b)["label"].get<std::string>(), "b0 ([[0,1,2],[3,4]])");

  // A missing argument is dropped, and the leftover spacing tidied; with nothing left the label
  // falls back rather than reading "()".
  const std::string c = tree.Create("ndmspc/ngnt/map", json::object(), b, "");
  EXPECT_EQ(tree.Get(c)["label"].get<std::string>(), "map");
  const std::string d = tree.Create("ndmspc/ngnt/map", {{"mappingPad", "pad2"}}, b, "");
  EXPECT_EQ(tree.Get(d)["label"].get<std::string>(), "pad2");

  // An action that declares no template keeps the guessed label.
  const std::string e = tree.Create("ndmspc/ngnt/point", {{"contentPad", "pad9"}}, c, "");
  EXPECT_EQ(tree.Get(e)["label"].get<std::string>(), "pad9");

  Ndmspc::gNdmspcMcpTools = previous;
}

TEST(NInstanceTreeTest, ARepeatedStartIsOneSessionAndANameIsNotTheNodes)
{
  json          store;
  NInstanceTree tree(store);

  // The same action with the same arguments is the same session, so a repeated run reuses the node
  // rather than growing a twin beside it.
  const std::string first = tree.Create("ndmspc/browser/open", {{"file", "a.root"}}, "", "");
  EXPECT_EQ(tree.Create("ndmspc/browser/open", {{"file", "a.root"}}, "", ""), first);

  // What a node is called is the action's own business - a tool's template, or the arguments guessed.
  // A session's *name* is the room's and is held apart from this (see NHttpServer::SessionList), so a
  // tool that opens no file, and a session called something that is not a file, work the same way.
  EXPECT_EQ(tree.Get(first)["label"].get<std::string>(), "a.root");

  const std::string other = tree.Create("a-tool/start", {{"store", "s1"}}, "", "");
  EXPECT_EQ(other, "i2");
  EXPECT_EQ(tree.Get(other)["label"].get<std::string>(), "s1");
  EXPECT_EQ(Ndmspc::NInstanceTree::GroupOf(tree.Action(other)), "a-tool");
}

TEST(NInstanceTreeTest, SetParamsFillsANodeAndKeepsItsId)
{
  json          store;
  NInstanceTree tree(store);

  // A session started with nothing but its token: the node is there, with no arguments yet.
  const std::string id = tree.Create("ndmspc/browser/open", {{"session", "t1"}}, "", "");
  EXPECT_EQ(tree.Params(id).size(), 1u);

  // Its first step fills it: the arguments it ran with, and the token it keeps, under the same id, so
  // whatever refers to the session - the room's active path, a pad's path - goes on doing so.
  tree.SetParams(id, {{"session", "t1"}, {"file", "hsimple.root"}});
  EXPECT_EQ(id, "i1");
  EXPECT_EQ(tree.Params(id)["file"].get<std::string>(), "hsimple.root");

  // ... and it is called by what those arguments say.
  EXPECT_EQ(tree.Get(id)["label"].get<std::string>(), "hsimple.root");

  // Running that same step again finds the very node it filled, rather than a twin beside it.
  EXPECT_EQ(tree.Create("ndmspc/browser/open", {{"session", "t1"}, {"file", "hsimple.root"}}, "", ""), id);
}
