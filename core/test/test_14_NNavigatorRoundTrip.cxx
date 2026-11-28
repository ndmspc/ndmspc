#include <gtest/gtest.h>

#include <TFile.h>
#include <TString.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "05_single_bining/NSingleBinning01Gaus.C"
#include "ndmspc/core/NGnNavigator.h"
#include "ndmspc/core/NGnTree.h"

namespace {

std::string ReadFile(const std::string & name)
{
  std::ifstream     f(name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

/**
 * A reshaped navigator must survive being written to a file and read back.
 *
 * The reshape cache stores a navigator rather than reshaping again, so everything downstream rests on
 * this: `fChildren` is `std::vector<NGnNavigator *>` and `fParent` points back up, which ROOT has to
 * stream as a graph rather than by value, and `fProjection`/`fObjectContentMap` carry live histograms.
 * The navigator's own tree is transient (`///<!`), so a loaded one has no tree until the caller
 * re-attaches it — which is what `SetGnTreeRecursive` is for.
 */
class NNavigatorRoundTripTest : public ::testing::Test {
  protected:
  static const std::string testFile;

  static void SetUpTestSuite()
  {
    std::ifstream f(testFile);
    if (!f.good()) NSingleBinning01Gaus(testFile);
  }
  static void TearDownTestSuite() { std::remove(testFile.c_str()); }
};

const std::string NNavigatorRoundTripTest::testFile = "test_NNavigatorRoundTrip.root";

} // namespace

/** Writing a navigator and reading it back yields the same navigator. */
TEST_F(NNavigatorRoundTripTest, WrittenNavigatorReadsBackIdentical)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_FALSE(ngnt->IsZombie());

  Ndmspc::NGnNavigator * before = ngnt->Reshape("", {{0}, {1, 2}});
  ASSERT_TRUE(before);
  ASSERT_FALSE(before->GetChildren().empty());

  const std::string beforeJson = "roundtrip_before.json";
  before->Export(beforeJson, {});
  const std::string expected = ReadFile(beforeJson);

  // Write it the way NGnNavigator::Export does for a .root name.
  {
    TFile * out = TFile::Open("roundtrip_nav.root", "RECREATE");
    ASSERT_TRUE(out);
    ASSERT_FALSE(out->IsZombie());
    out->cd();
    before->Write("nav");
    out->Close();
    delete out;
  }

  // Read it back.
  TFile * in = TFile::Open("roundtrip_nav.root");
  ASSERT_TRUE(in);
  TObject * readBack = in->Get("nav");
  ASSERT_NE(readBack, nullptr) << "the navigator did not survive the file round-trip";
  ASSERT_NE(dynamic_cast<Ndmspc::NGnNavigator *>(readBack), nullptr) << "read back as " << readBack->ClassName();

  auto * after = dynamic_cast<Ndmspc::NGnNavigator *>(readBack);

  EXPECT_EQ(after->GetChildren().size(), before->GetChildren().size());
  EXPECT_EQ(after->GetNLevels(), before->GetNLevels());
  EXPECT_EQ(after->GetLevel(), before->GetLevel());
  EXPECT_NE(after->GetProjection(), nullptr) << "the projection histogram was not restored";

  // The tree is transient by design: the caller re-attaches it (this is what the cache does).
  EXPECT_EQ(after->GetGnTree(), nullptr) << "fGnTree should be transient, not streamed";
  after->SetGnTreeRecursive(ngnt);

  const std::string afterJson = "roundtrip_after.json";
  after->Export(afterJson, {});
  EXPECT_EQ(ReadFile(afterJson), expected) << "the navigator changed across the round-trip";

  in->Close();
  delete in;
  std::remove("roundtrip_nav.root");
  std::remove(beforeJson.c_str());
  std::remove(afterJson.c_str());
  delete before;
  ngnt->Close();
}

/** Children carry a usable parent link after a round-trip, so walking back up works. */
TEST_F(NNavigatorRoundTripTest, ChildrenKnowTheirParentsAfterRoundTrip)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  Ndmspc::NGnNavigator * before = ngnt->Reshape("", {{0}, {1, 2}});
  ASSERT_TRUE(before);

  {
    TFile * out = TFile::Open("roundtrip_parents.root", "RECREATE");
    ASSERT_TRUE(out);
    out->cd();
    before->Write("nav");
    out->Close();
    delete out;
  }

  TFile * in = TFile::Open("roundtrip_parents.root");
  ASSERT_TRUE(in);
  auto * after = dynamic_cast<Ndmspc::NGnNavigator *>(in->Get("nav"));
  ASSERT_NE(after, nullptr);

  int checked = 0;
  for (auto * child : after->GetChildren()) {
    if (child == nullptr) continue;
    EXPECT_EQ(child->GetParent(), after) << "a child lost its parent in the round-trip";
    checked++;
  }
  EXPECT_GT(checked, 0);

  in->Close();
  delete in;
  std::remove("roundtrip_parents.root");
  delete before;
  ngnt->Close();
}
