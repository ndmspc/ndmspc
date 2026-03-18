#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include "05_nested/NNested01Gaus.C"
#include "NGnNavigator.h"
#include "NGnTree.h"

class NNested01GausTest : public ::testing::Test {
  protected:
  std::string testFile     = "test_NNested01Gaus.root";
  std::string testJsonFile = "test.json";

  void SetUp() override
  {
    // Only create the test file if it doesn't exist
    std::ifstream f(testFile);
    if (!f.good()) {
      NNested01Gaus(testFile);
    }
    f.close();
  }

  void TearDown() override
  {
    std::remove(testFile.c_str());
    // Do not remove testFile here to allow reuse between tests
  }
};

TEST_F(NNested01GausTest, CreatesOutputFile)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_TRUE(!ngnt->IsZombie());

  ASSERT_EQ(ngnt->GetBinning()->GetAxes().size(), 3);
  ASSERT_EQ(ngnt->GetStorageTree()->GetTree()->GetEntries(), 150);

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", {{0, 1, 2}});
  ASSERT_TRUE(nav);
  nav->Export(testJsonFile, {}, "");
  // Check that the file was created
  std::ifstream f(testJsonFile);
  ASSERT_TRUE(f.good()) << "Output file was not created";
  f.close();
  delete nav;

  ngnt->Close();
}

TEST_F(NNested01GausTest, ReshapeMultipleLevels)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_TRUE(!ngnt->IsZombie());

  // Try different combinations of axes
  std::vector<std::vector<std::vector<int>>> axes_combinations = {
      {{0, 1, 2}}, {{2, 1, 0}}, {{0}, {1, 2}}, {{0, 1}, {2}}};

  for (const auto & axes : axes_combinations) {
    Ndmspc::NGnNavigator * nav = ngnt->Reshape("", axes);
    ASSERT_TRUE(nav);
    nav->Export(testJsonFile, {}, "");
    std::ifstream f(testJsonFile);
    ASSERT_TRUE(f.good()) << "Output file was not created";
    f.close();
    delete nav;
    std::remove(testJsonFile.c_str());
  }

  ngnt->Close();
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
