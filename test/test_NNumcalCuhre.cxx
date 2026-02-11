#include <cmath>

#include <gtest/gtest.h>

#include "NNumcalManager.h"

namespace {

TEST(NNumcalManager, CuhreExample) {
  auto result = Ndmspc::NNumcalManager::ExampleCuhre();

  ASSERT_EQ(result.integral.size(), 1u);
  ASSERT_EQ(result.error.size(), 1u);
  ASSERT_EQ(result.prob.size(), 1u);

  const double expected = 0.25;
  const double tolerance = 0.02;
  EXPECT_TRUE(std::isfinite(result.integral[0]));
  EXPECT_NEAR(result.integral[0], expected, tolerance);
}

}  // namespace
