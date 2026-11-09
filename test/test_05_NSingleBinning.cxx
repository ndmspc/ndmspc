#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>
#include "05_single_bining/NSingleBinning01Gaus.C"
#include "ndmspc/core/NGnNavigator.h"
#include "ndmspc/core/NGnTree.h"

class NSingleBinning01GausTest : public ::testing::Test {
  protected:
  static const std::string testFile;
  std::string              testJsonFile = "test.json";

  // The macro is deterministic, so build the tree once for the whole suite
  // instead of once per test (5 tests would otherwise each regenerate it).
  static void SetUpTestSuite()
  {
    // Only create the test file if it doesn't exist
    std::ifstream f(testFile);
    if (!f.good()) {
      NSingleBinning01Gaus(testFile);
    }
  }

  static void TearDownTestSuite() { std::remove(testFile.c_str()); }
};

const std::string NSingleBinning01GausTest::testFile = "test_NSingleBinning01Gaus.root";

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

/**
 * A parameter's displayed range is the spread of its valid cells at that level — mean ± 3·rms — so empty
 * cells (a zero) and outliers cannot stretch it, and cells that all agree still get a span.
 */
TEST_F(NSingleBinning01GausTest, ParameterRangeIsMeanPlusMinusThreeRms)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", {{0}, {1, 2}});
  ASSERT_TRUE(nav);

  nlohmann::json j;
  nav->ExportToJson(j, nav, {});
  ASSERT_TRUE(j.contains("fArrays"));

  for (auto & [param, arr] : j["fArrays"].items()) {
    ASSERT_TRUE(arr.contains("min")) << param;
    ASSERT_TRUE(arr.contains("max")) << param;
    if (!arr.contains("values") || !arr["values"].is_array()) continue; // a level with no values: children

    std::vector<double> values;
    for (const auto & value : arr["values"]) {
      if (!value.is_number()) continue;
      const double v = value.get<double>();
      if (std::isnan(v) || std::fabs(v) <= 1e-12) continue;
      values.push_back(v);
    }
    if (values.empty()) {
      EXPECT_DOUBLE_EQ(arr["min"].get<double>(), 0.0) << param;
      EXPECT_DOUBLE_EQ(arr["max"].get<double>(), 0.0) << param;
      continue;
    }
    double sum = 0.0, sumSq = 0.0;
    for (const double v : values) {
      sum += v;
      sumSq += v * v;
    }
    const double n    = static_cast<double>(values.size());
    const double mean = sum / n;
    const double rms  = std::sqrt(std::max(0.0, sumSq / n - mean * mean));
    if (rms > 0.0) {
      EXPECT_NEAR(arr["min"].get<double>(), mean - 3.0 * rms, 1e-9) << param;
      EXPECT_NEAR(arr["max"].get<double>(), mean + 3.0 * rms, 1e-9) << param;
    }
    else {
      EXPECT_LT(arr["min"].get<double>(), mean) << param;
      EXPECT_GT(arr["max"].get<double>(), mean) << param;
    }

    // The errors get the same computation on their own numbers (their own mean and rms).
    if (!arr.contains("errors") || !arr["errors"].is_array()) continue;
    std::vector<double> errors;
    for (const auto & error : arr["errors"]) {
      if (!error.is_number()) continue;
      const double e = error.get<double>();
      if (std::isnan(e) || std::fabs(e) <= 1e-12) continue;
      errors.push_back(e);
    }
    ASSERT_TRUE(arr.contains("minE")) << param;
    ASSERT_TRUE(arr.contains("maxE")) << param;
    if (errors.empty()) {
      EXPECT_DOUBLE_EQ(arr["minE"].get<double>(), 0.0) << param;
      EXPECT_DOUBLE_EQ(arr["maxE"].get<double>(), 0.0) << param;
      continue;
    }
    double sumE = 0.0, sumSqE = 0.0;
    for (const double e : errors) {
      sumE += e;
      sumSqE += e * e;
    }
    const double nE    = static_cast<double>(errors.size());
    const double meanE = sumE / nE;
    const double rmsE  = std::sqrt(std::max(0.0, sumSqE / nE - meanE * meanE));
    if (rmsE > 0.0) {
      EXPECT_NEAR(arr["minE"].get<double>(), std::max(0.0, meanE - 3.0 * rmsE), 1e-9) << param;
      EXPECT_NEAR(arr["maxE"].get<double>(), meanE + 3.0 * rmsE, 1e-9) << param;
    }
    else {
      EXPECT_GE(arr["minE"].get<double>(), 0.0) << param;
      EXPECT_GT(arr["maxE"].get<double>(), 0.0) << param;
    }
  }

  delete nav;
  ngnt->Close();
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

