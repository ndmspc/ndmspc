#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include "05_single_bining/NSingleBinning01Gaus.C"
#include "ndmspc/core/NGnNavigator.h"
#include "ndmspc/core/NGnTree.h"

class NSingleBinning01GausTest : public ::testing::Test {
  protected:
  std::string testFile     = "test_NSingleBinning01Gaus.root";
  std::string testJsonFile = "test.json";

  void SetUp() override
  {
    // Only create the test file if it doesn't exist
    std::ifstream f(testFile);
    if (!f.good()) {
      NSingleBinning01Gaus(testFile);
    }
    f.close();
  }

  void TearDown() override
  {
    std::remove(testFile.c_str());
    // Do not remove testFile here to allow reuse between tests
  }
};

TEST_F(NSingleBinning01GausTest, CreatesOutputFile)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_TRUE(!ngnt->IsZombie());

  ASSERT_EQ(ngnt->GetBinning()->GetAxes().size(), 3);
  ASSERT_EQ(ngnt->GetStorageTree()->GetTree()->GetEntries(), 150);

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", {{0, 1, 2}});
  ASSERT_TRUE(nav);
  nav->Export(testJsonFile, {});
  // Check that the file was created
  std::ifstream f(testJsonFile);
  ASSERT_TRUE(f.good()) << "Output file was not created";
  f.close();
  delete nav;

  ngnt->Close();
}

TEST_F(NSingleBinning01GausTest, ReshapeMultipleLevels)
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
    nav->Export(testJsonFile, {});
    std::ifstream f(testJsonFile);
    ASSERT_TRUE(f.good()) << "Output file was not created";
    f.close();
    delete nav;
    std::remove(testJsonFile.c_str());
  }

  ngnt->Close();
}

namespace {
std::pair<double, double> MeanAndMeanError(const nlohmann::json & values, const nlohmann::json & errors)
{
  if (!values.is_array()) return {0.0, 0.0};
  double sumValues    = 0.0;
  double sumVariances = 0.0;
  size_t n            = 0;
  for (size_t k = 0; k < values.size(); k++) {
    if (!values[k].is_number()) continue;
    double v = values[k].get<double>();
    if (v == 0.0 || std::isnan(v)) continue;
    sumValues += v;
    double variance = 0.0;
    if (errors.is_array() && k < errors.size() && errors[k].is_number()) {
      variance = errors[k].get<double>();
      if (std::isnan(variance) || variance < 0.0) variance = 0.0;
    }
    sumVariances += variance;
    n++;
  }
  if (n == 0) return {0.0, 0.0};
  return {sumValues / static_cast<double>(n), sumVariances / (static_cast<double>(n) * static_cast<double>(n))};
}
} // namespace

TEST_F(NSingleBinning01GausTest, AveragesHigherLevelParameterValues)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_TRUE(!ngnt->IsZombie());

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", {{0}, {1, 2}});
  ASSERT_TRUE(nav);

  std::string jsonFile = "test_NSingleBinning01Gaus_avg.json";
  nav->Export(jsonFile, {});

  nlohmann::json j = nlohmann::json::parse(std::ifstream(jsonFile));
  ASSERT_TRUE(j.contains("fArrays"));
  ASSERT_TRUE(j.contains("children"));
  ASSERT_TRUE(j["children"].contains("content"));
  const auto & kids = j["children"]["content"];
  ASSERT_FALSE(kids.empty());

  for (auto & [param, arr] : j["fArrays"].items()) {
    ASSERT_TRUE(arr.contains("values"));
    ASSERT_TRUE(arr.contains("errors"));
    ASSERT_EQ(arr["values"].size(), kids.size());
    for (size_t i = 0; i < kids.size(); i++) {
      const auto & child = kids[i];
      if (child.is_null() || !child.contains("fArrays") || !child["fArrays"].contains(param)) continue;
      const auto & childArr = child["fArrays"][param];
      ASSERT_TRUE(childArr.contains("values"));
      auto [mean, error] = MeanAndMeanError(childArr["values"], childArr.contains("errors") ? childArr["errors"] : nlohmann::json());
      EXPECT_NEAR(arr["values"][i].get<double>(), mean, 1e-9);
      EXPECT_NEAR(arr["errors"][i].get<double>(), error, 1e-15);
    }
  }

  std::remove(jsonFile.c_str());
  delete nav;
  ngnt->Close();
}

TEST_F(NSingleBinning01GausTest, AveragesDisabledByMember)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_TRUE(!ngnt->IsZombie());

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", {{0}, {1, 2}});
  ASSERT_TRUE(nav);
  ASSERT_TRUE(nav->GetAverageParameters()); // on by default

  nav->SetAverageParameters(false);

  nlohmann::json j;
  nav->ExportToJson(j, nav, {});
  ASSERT_TRUE(j.contains("fArrays"));
  for (auto & [param, arr] : j["fArrays"].items()) {
    EXPECT_FALSE(arr.contains("values")) << "param " << param;
    EXPECT_FALSE(arr.contains("errors")) << "param " << param;
    EXPECT_TRUE(arr.contains("min"));
    EXPECT_TRUE(arr.contains("max"));
  }

  delete nav;
  ngnt->Close();
}

TEST_F(NSingleBinning01GausTest, AveragesDisabledByCfgOverridesMember)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  ASSERT_TRUE(!ngnt->IsZombie());

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", {{0}, {1, 2}});
  ASSERT_TRUE(nav);
  nav->SetAverageParameters(true); // cfg must still be able to disable it

  nlohmann::json j;
  nav->ExportToJson(j, nav, {}, {{"averages", false}});
  ASSERT_TRUE(j.contains("fArrays"));
  for (auto & [param, arr] : j["fArrays"].items()) {
    EXPECT_FALSE(arr.contains("values")) << "param " << param;
    EXPECT_FALSE(arr.contains("errors")) << "param " << param;
  }

  delete nav;
  ngnt->Close();
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

