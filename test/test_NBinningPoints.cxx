/**
 * Prefilled binning points: filling only the combinations a sparse tree actually has.
 *
 * A definition's axes describe a product — `{{1}}` on each of three directory axes is every
 * label of each, so 5.8M points for a tree holding 23. `AddBinningDefinition`'s points overload
 * (and `FillPoints`) pins those axes to the tuples that exist; an axis a point does not reach is
 * still expanded, so the content axes a real import adds keep their product.
 *
 * `FillAll` and `AddBinningDefinition(name, binning)` must keep working exactly as before: the
 * all-full-range points are the same fill, and that equivalence is pinned below.
 */
#include <gtest/gtest.h>

#include <TAxis.h>
#include <TObjArray.h>

#include <string>
#include <vector>

#include "ndmspc/core/NBinning.h"
#include "ndmspc/core/NBinningDef.h"
#include "ndmspc/core/NGnTree.h"
#include "ndmspc/core/NUtils.h"

using Ndmspc::NBinning;
using Ndmspc::NBinningDef;
using Ndmspc::NBinningPointAxis;
using Ndmspc::NGnTree;

namespace {

/** An NGnTree over label axes, as a directory import leaves it. */
NGnTree * MakeTree(const std::vector<std::pair<std::string, std::vector<std::string>>> & axes)
{
  TObjArray * list = new TObjArray();
  for (const auto & one : axes) {
    list->Add(Ndmspc::NUtils::CreateAxisFromLabels(one.first, one.first, one.second));
  }
  return new NGnTree(list, "/tmp/test_NBinningPoints_ngnt.root");
}

/** `b[axis] = {{1}}` — every label of every axis is its own bin. */
std::map<std::string, std::vector<std::vector<int>>>
EveryLabel(const std::vector<std::pair<std::string, std::vector<std::string>>> & axes)
{
  std::map<std::string, std::vector<std::vector<int>>> b;
  for (const auto & one : axes) {
    b[one.first] = {{1}};
  }
  return b;
}

const std::vector<std::pair<std::string, std::vector<std::string>>> kThree = {
    {"dca", {"dca_0p05", "dca_0p5"}},
    {"pid", {"default", "tpc_2", "tpc_3"}},
    {"vtxz", {"vtxz_8", "vtxz_10"}},
};

} // namespace

TEST(NBinningPoints, SparseLabelsFillOnlyThePointsGiven)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);
  ASSERT_FALSE(ngnt->IsZombie());

  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel(kThree),
      {{NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"), NBinningPointAxis::Label("vtxz_8")},
       {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Label("tpc_2"), NBinningPointAxis::Label("vtxz_10")},
       {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("tpc_3"), NBinningPointAxis::Label("vtxz_10")}});

  NBinningDef * def = ngnt->GetBinning()->GetDefinition("default");
  ASSERT_TRUE(def != nullptr);
  // The product of the labels is 2*3*2 = 12; only the three given points are real.
  EXPECT_EQ(def->GetIds().size(), 3u);

  delete ngnt;
}

TEST(NBinningPoints, TheFullProductIsStillWhatThePlainOverloadFills)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  ngnt->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree));

  NBinningDef * def = ngnt->GetBinning()->GetDefinition("default");
  ASSERT_TRUE(def != nullptr);
  EXPECT_EQ(def->GetIds().size(), 12u);

  delete ngnt;
}

TEST(NBinningPoints, AllFullRangesFillExactlyWhatFillAllFills)
{
  // The design anchor: a point of full ranges is the whole product, so the two forms agree.
  NGnTree * plain = MakeTree(kThree);
  NGnTree * point = MakeTree(kThree);
  ASSERT_TRUE(plain != nullptr && point != nullptr);

  plain->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree));
  point->GetBinning()->AddBinningDefinition(
      "default", EveryLabel(kThree),
      {{NBinningPointAxis::All(), NBinningPointAxis::All(), NBinningPointAxis::All()}});

  const auto a = plain->GetBinning()->GetDefinition("default")->GetIds();
  const auto b = point->GetBinning()->GetDefinition("default")->GetIds();
  EXPECT_EQ(a, b);

  delete plain;
  delete point;
}

TEST(NBinningPoints, ATrailingAxisThePointOmitsIsExpanded)
{
  // The import's shape: directory axes pinned by label, the content axis appended later and open.
  NGnTree * ngnt = MakeTree({{"dca", {"dca_0p05", "dca_0p5"}},
                              {"pid", {"default", "tpc_2", "tpc_3"}},
                              {"content", {"a", "b", "c", "d"}}});
  ASSERT_TRUE(ngnt != nullptr);

  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"dca_0p05", "dca_0p5"}}, {"pid", {"default", "tpc_2", "tpc_3"}},
                              {"content", {"a", "b", "c", "d"}}}),
      {{NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default")},
       {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Label("tpc_2")},
       {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("tpc_3")}});

  NBinningDef * def = ngnt->GetBinning()->GetDefinition("default");
  ASSERT_TRUE(def != nullptr);
  // 3 pinned points x the 4 content bins they leave open.
  EXPECT_EQ(def->GetIds().size(), 3u * 4u);

  delete ngnt;
}

TEST(NBinningPoints, PointsDoNotMultiplyAgainstEachOther)
{
  NGnTree * ngnt = MakeTree({{"dca", {"a", "b"}}, {"pid", {"x", "y", "z"}}});
  ASSERT_TRUE(ngnt != nullptr);

  // a x all-pid (3) plus b x pid y (1) -- 4, not 2*3.
  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"a", "b"}}, {"pid", {"x", "y", "z"}}}),
      {{NBinningPointAxis::Label("a"), NBinningPointAxis::All()},
       {NBinningPointAxis::Label("b"), NBinningPointAxis::Label("y")}});

  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 4u);
  delete ngnt;
}

TEST(NBinningPoints, OverlappingPointsFillABinOnce)
{
  NGnTree * ngnt = MakeTree({{"dca", {"a", "b"}}, {"pid", {"x", "y", "z"}}});
  ASSERT_TRUE(ngnt != nullptr);

  // The second point's bin is already inside the first point's product.
  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"a", "b"}}, {"pid", {"x", "y", "z"}}}),
      {{NBinningPointAxis::Label("a"), NBinningPointAxis::All()},
       {NBinningPointAxis::Label("a"), NBinningPointAxis::Bin(3)}});

  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 3u);
  delete ngnt;
}

TEST(NBinningPoints, ARangeExpandsWithinItsPointOnly)
{
  NGnTree * ngnt = MakeTree({{"dca", {"a1", "a2", "a3"}}, {"pid", {"x", "y"}}});
  ASSERT_TRUE(ngnt != nullptr);

  // Bins 1..3 of the first axis x all of the second: 6, and a second point adds its own 1.
  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"a1", "a2", "a3"}}, {"pid", {"x", "y"}}}),
      {{NBinningPointAxis::Bins(1, 3), NBinningPointAxis::All()},
       {NBinningPointAxis::All(), NBinningPointAxis::Label("y")}});

  // 3*2 = 6 for the first point; the second (all x y) is already covered by it.
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 6u);
  delete ngnt;
}

TEST(NBinningPoints, AnOpenEndIsFromHereOnOrUpToHere)
{
  NGnTree * ngnt = MakeTree({{"dca", {"a1", "a2", "a3", "a4", "a5"}}, {"pid", {"x"}}});
  ASSERT_TRUE(ngnt != nullptr);

  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"a1", "a2", "a3", "a4", "a5"}}, {"pid", {"x"}}}),
      {{NBinningPointAxis::Bins(3, NBinningPointAxis::OPEN)}});
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 3u); // a3, a4, a5

  delete ngnt;

  NGnTree * head = MakeTree({{"dca", {"a1", "a2", "a3", "a4", "a5"}}, {"pid", {"x"}}});
  ASSERT_TRUE(head != nullptr);
  head->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"a1", "a2", "a3", "a4", "a5"}}, {"pid", {"x"}}}),
      {{NBinningPointAxis::Bins(NBinningPointAxis::OPEN, 2)}});
  EXPECT_EQ(head->GetBinning()->GetDefinition("default")->GetIds().size(), 2u); // a1, a2
  delete head;
}

TEST(NBinningPoints, AnUnknownLabelIsSkippedNotFatal)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel(kThree),
      {{NBinningPointAxis::Label("no-such-label"), NBinningPointAxis::Label("default"),
        NBinningPointAxis::Label("vtxz_8")},
       {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("tpc_2"),
        NBinningPointAxis::Label("vtxz_10")}});

  // The unmatched point contributes nothing; the good one still fills.
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 1u);
  delete ngnt;
}

TEST(NBinningPoints, ADuplicatePointAppearsOnce)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  const std::vector<NBinningPointAxis> one = {NBinningPointAxis::Label("dca_0p05"),
                                              NBinningPointAxis::Label("default"),
                                              NBinningPointAxis::Label("vtxz_8")};
  ngnt->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree), {one, one});

  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 1u);
  delete ngnt;
}

TEST(NBinningPoints, AppendPointsGrowsTheDefinition)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel(kThree),
      {{NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"),
        NBinningPointAxis::Label("vtxz_8")},
       {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Label("tpc_2"),
        NBinningPointAxis::Label("vtxz_10")}});

  NBinningDef * def = ngnt->GetBinning()->GetDefinition("default");
  ASSERT_TRUE(def != nullptr);
  ASSERT_EQ(def->GetIds().size(), 2u);
  const std::vector<Long64_t> before = def->GetIds();

  // One already held, two new.
  const Long64_t added = ngnt->GetBinning()->AppendPoints(
      "default",
      {{NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"),
        NBinningPointAxis::Label("vtxz_8")},
       {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("tpc_2"),
        NBinningPointAxis::Label("vtxz_10")},
       {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Label("tpc_3"),
        NBinningPointAxis::Label("vtxz_8")}});

  EXPECT_EQ(added, 2);
  EXPECT_EQ(def->GetIds().size(), 4u);
  // The order the definition already had is kept; the new ids follow.
  for (size_t i = 0; i < before.size(); i++) {
    EXPECT_EQ(def->GetIds()[i], before[i]);
  }

  delete ngnt;
}

TEST(NBinningPoints, AppendPointsIsIdempotent)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  const std::vector<std::vector<NBinningPointAxis>> points = {
      {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"),
       NBinningPointAxis::Label("vtxz_8")}};

  ngnt->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree), points);
  ASSERT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 1u);

  // The map is cleared once a fill completes, so this also proves it is rebuilt from the definition.
  EXPECT_EQ(ngnt->GetBinning()->AppendPoints("default", points), 0);
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 1u);

  delete ngnt;
}

TEST(NBinningPoints, AppendPointsRefusesAnUnknownName)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  ngnt->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree),
                                           std::vector<std::vector<NBinningPointAxis>>{});
  EXPECT_EQ(ngnt->GetBinning()->AppendPoints("no-such-definition",
                                             {{NBinningPointAxis::Label("dca_0p05")}}),
            0);

  delete ngnt;
}

TEST(NBinningPoints, AValueRangeResolvesToBins)
{
  NGnTree * ngnt = MakeTree({{"dca", {"0.0", "1.0", "2.0", "3.0"}}, {"pid", {"x"}}});
  ASSERT_TRUE(ngnt != nullptr);

  // Bin 1 covers [0,1), bin 2 [1,2), bin 3 [2,3), bin 4 [3,4); [0.5, 2.5] meets bins 1..3.
  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel({{"dca", {"0.0", "1.0", "2.0", "3.0"}}, {"pid", {"x"}}}),
      {{NBinningPointAxis::Range(0.5, 2.5)}});

  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 3u);
  delete ngnt;
}

TEST(NBinningPoints, FillInRunsFillsWhatOneRunWouldInSeveralExecutors)
{
  // The point of chunking: a large product need not be one uninterruptible executor run.
  NGnTree * single = MakeTree(kThree);
  NGnTree * chunked = MakeTree(kThree);
  ASSERT_TRUE(single != nullptr && chunked != nullptr);

  single->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree));
  // Declared with no points, so the definition starts empty and the runs do the filling.
  chunked->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree),
                                              std::vector<std::vector<NBinningPointAxis>>{});
  ASSERT_EQ(chunked->GetBinning()->GetDefinition("default")->GetIds().size(), 0u);

  // 12 bins, two outermost entries per run -> 6 runs.
  const Long64_t filled = chunked->GetBinning()->FillInRuns("default", 2);
  EXPECT_EQ(filled, 12);
  EXPECT_EQ(chunked->GetBinning()->GetDefinition("default")->GetIds(),
            single->GetBinning()->GetDefinition("default")->GetIds());

  delete single;
  delete chunked;
}

TEST(NBinningPoints, FillInRunsOfOneEntryStillCoversEverything)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);
  ngnt->GetBinning()->AddBinningDefinition("default", EveryLabel(kThree),
                                           std::vector<std::vector<NBinningPointAxis>>{});

  // One outermost entry per run: as many runs as that axis has entries.
  EXPECT_EQ(ngnt->GetBinning()->FillInRuns("default", 1), 12);
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 12u);
  delete ngnt;
}

TEST(NBinningPoints, AStoppedFillCanBeResumedByAppending)
{
  NGnTree * ngnt = MakeTree(kThree);
  ASSERT_TRUE(ngnt != nullptr);

  // Half the points, as a run that stopped early would have left it.
  ngnt->GetBinning()->AddBinningDefinition(
      "default", EveryLabel(kThree),
      {{NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"), NBinningPointAxis::Label("vtxz_8")},
       {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Label("tpc_2"), NBinningPointAxis::Label("vtxz_10")}});
  ASSERT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 2u);

  // Resuming adds only what is new; the points already held are skipped, not repeated.
  const std::vector<std::vector<NBinningPointAxis>> everything = {
      {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"), NBinningPointAxis::Label("vtxz_8")},
      {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Label("tpc_2"), NBinningPointAxis::Label("vtxz_10")},
      {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("tpc_3"), NBinningPointAxis::Label("vtxz_10")}};
  EXPECT_EQ(ngnt->GetBinning()->AppendPoints("default", everything), 1);
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 3u);

  // Resuming a completed fill changes nothing.
  EXPECT_EQ(ngnt->GetBinning()->AppendPoints("default", everything), 0);
  EXPECT_EQ(ngnt->GetBinning()->GetDefinition("default")->GetIds().size(), 3u);

  delete ngnt;
}

TEST(NBinningPoints, ResolvePointReportsTheEntriesASelectionCovers)
{
  NGnTree * ngnt = MakeTree({{"dca", {"a1", "a2", "a3", "a4"}}, {"pid", {"x", "y"}}});
  ASSERT_TRUE(ngnt != nullptr);
  ngnt->GetBinning()->AddBinningDefinition("default", EveryLabel({{"dca", {"a1", "a2", "a3", "a4"}},
                                                                 {"pid", {"x", "y"}}}));

  // All entries of both axes, as 1-based indices.
  EXPECT_EQ(ngnt->GetBinning()->ResolvePoint({NBinningPointAxis::All(), NBinningPointAxis::All()}),
            (std::vector<std::vector<int>>{{1, 2, 3, 4}, {1, 2}}));
  // One bin, and one bin range.
  EXPECT_EQ(ngnt->GetBinning()->ResolvePoint({NBinningPointAxis::Bin(2)}),
            (std::vector<std::vector<int>>{{2}, {1, 2}}));
  EXPECT_EQ(ngnt->GetBinning()->ResolvePoint({NBinningPointAxis::Bins(2, 3)}),
            (std::vector<std::vector<int>>{{2, 3}, {1, 2}}));
  // An omitted axis is taken whole.
  EXPECT_EQ(ngnt->GetBinning()->ResolvePoint({}), (std::vector<std::vector<int>>{{1, 2, 3, 4}, {1, 2}}));

  delete ngnt;
}

TEST(NBinningPoints, AxisSelectionsAreReadableFromJson)
{
  bool ok = false;

  EXPECT_EQ(Ndmspc::PointAxisFromJson(json("dca_0p05"), &ok), NBinningPointAxis::Label("dca_0p05"));
  EXPECT_TRUE(ok);

  // "*" and "all" are the whole axis, but a real label naming "all" is spelled as an object.
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json("*"), &ok), NBinningPointAxis::All());
  EXPECT_TRUE(ok);
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json("all"), &ok), NBinningPointAxis::All());
  EXPECT_TRUE(ok);
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json{{"label", "all"}}, &ok), NBinningPointAxis::Label("all"));
  EXPECT_TRUE(ok);

  EXPECT_EQ(Ndmspc::PointAxisFromJson(json(3), &ok), NBinningPointAxis::Bin(3));
  EXPECT_TRUE(ok);

  // Bin ranges, including the open-ended spellings.
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json::array({2, 5}), &ok), NBinningPointAxis::Bins(2, 5));
  EXPECT_TRUE(ok);
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json::array({2, nullptr}), &ok),
            NBinningPointAxis::Bins(2, NBinningPointAxis::OPEN));
  EXPECT_TRUE(ok);
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json::array({nullptr, 5}), &ok),
            NBinningPointAxis::Bins(NBinningPointAxis::OPEN, 5));
  EXPECT_TRUE(ok);
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json::array({2}), &ok),
            NBinningPointAxis::Bins(2, NBinningPointAxis::OPEN));
  EXPECT_TRUE(ok);
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json::array(), &ok), NBinningPointAxis::All());
  EXPECT_TRUE(ok);

  EXPECT_EQ(Ndmspc::PointAxisFromJson(json{{"from", 0.5}, {"to", 1.5}}, &ok), NBinningPointAxis::Range(0.5, 1.5));
  EXPECT_TRUE(ok);

  // A rebin descriptor is the definition vocabulary, and an unreadable value says so.
  EXPECT_EQ(Ndmspc::PointAxisFromJson(json{{"rebin", 2}, {"start", 1}, {"id", 3}}, &ok),
            NBinningPointAxis::Rebin(2, 1, 3));
  EXPECT_TRUE(ok);
  Ndmspc::PointAxisFromJson(json(true), &ok);
  EXPECT_FALSE(ok);
}

TEST(NBinningPoints, APointListIsReadableFromJson)
{
  // The shape a macro or tool hands over.
  const json points = json::array({json::array({"dca_0p05", "default", "vtxz_8"}),
                                   json::array({"dca_0p5", 3, json::array({nullptr, 2})})});

  EXPECT_EQ(Ndmspc::PointsFromJson(points),
            (std::vector<std::vector<NBinningPointAxis>>{
                {NBinningPointAxis::Label("dca_0p05"), NBinningPointAxis::Label("default"),
                 NBinningPointAxis::Label("vtxz_8")},
                {NBinningPointAxis::Label("dca_0p5"), NBinningPointAxis::Bin(3),
                 NBinningPointAxis::Bins(NBinningPointAxis::OPEN, 2)}}));

  // A point with an unreadable axis is dropped rather than half-read.
  EXPECT_EQ(Ndmspc::PointsFromJson(json::array({json::array({"a", true})})),
            (std::vector<std::vector<NBinningPointAxis>>{}));
}
