#include <cmath>

#include <gtest/gtest.h>

#include "NNumcalManager.h"

namespace {

TEST(NNumcalManager, VegasExample) {
  auto result = Ndmspc::NNumcalManager::ExampleVegas();

  ASSERT_EQ(result.GetIntegral().size(), 1u);
  ASSERT_EQ(result.GetError().size(), 1u);
  ASSERT_EQ(result.GetProb().size(), 1u);

  const double expected = 0.25;
  const double tolerance = 0.1;
  EXPECT_TRUE(std::isfinite(result.GetIntegral()[0]));
  EXPECT_NEAR(result.GetIntegral()[0], expected, tolerance);
}

}  // namespace
