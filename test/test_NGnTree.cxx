
#include <gtest/gtest.h>
#include <TFile.h>
#include <THnSparse.h>
#include "NGnTree.h"

class NGnTreeTest : public ::testing::Test {
  protected:
  Ndmspc::NGnTree * fObjEmpty{nullptr};
  Ndmspc::NGnTree * fObjCernStaff{nullptr};

  void SetUp() override
  {
    fObjEmpty = new Ndmspc::NGnTree(/* constructor args if any */);

    TFile * f = TFile::Open("cernstaff.root"); // Assuming this file exists and contains the necessary data
    if (!f || f->IsZombie()) {
      throw std::runtime_error("Failed to open cernstaff.root");
    }
    THnSparse * hSparse = dynamic_cast<THnSparse *>(f->Get("hsparse"));
    if (!hSparse) {
      throw std::runtime_error("THnSparse object 'hsparse' not found in cernstaff.root");
    }
    fObjCernStaff = new Ndmspc::NGnTree(hSparse->GetListOfAxes());
  }
  void TearDown() override
  {
    delete fObjEmpty;
    fObjEmpty = nullptr;
  }
};

TEST_F(NGnTreeTest, BinningIsNull)
{
  ASSERT_NE(fObjEmpty, nullptr);
  EXPECT_EQ(fObjEmpty->GetBinning(), nullptr);
}

TEST_F(NGnTreeTest, BinningIsNotNull)
{
  ASSERT_NE(fObjCernStaff, nullptr);
  EXPECT_NE(fObjCernStaff->GetBinning(), nullptr);
}

TEST_F(NGnTreeTest, GetBinning)
{
  ASSERT_NE(fObjCernStaff, nullptr);
  auto binning = fObjCernStaff->GetBinning();
  ASSERT_NE(binning, nullptr);
  EXPECT_EQ(binning->GetAxes().size(), 11);
}
