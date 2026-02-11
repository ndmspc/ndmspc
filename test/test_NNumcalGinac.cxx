#include <cmath>

#include <ginac/ginac.h>
#include <gtest/gtest.h>

#include "NNumcalManager.h"

namespace {

TEST(NNumcalManager, GiNaCVegas) {
  GiNaC::symbol x("x"), y("y");
  GiNaC::ex expr = GiNaC::sin(x) * y;

  Ndmspc::NNumcalManager mgr("ginac-test", "GiNaC + Vegas test");
  mgr.AddFunction([&expr, &x, &y](const std::vector<double>& vars) {
    if (vars.size() < 2) {
      return 0.0;
    }
    GiNaC::exmap repl;
    repl[x] = vars[0];
    repl[y] = vars[1];
    GiNaC::ex value = expr.subs(repl).evalf();
    return GiNaC::ex_to<GiNaC::numeric>(value).to_double();
  }, "sin(x)*y");

  Ndmspc::NNumcalManager::VegasOptions options;
  options.ndim = 2;
  options.maxeval = 20000;
  auto result = mgr.RunVegas(options);

  ASSERT_EQ(result.integral.size(), 1u);
  ASSERT_EQ(result.error.size(), 1u);
  ASSERT_EQ(result.prob.size(), 1u);

  const double expected = 0.5 * (1.0 - std::cos(1.0));
  const double tolerance = 0.1;
  EXPECT_TRUE(std::isfinite(result.integral[0]));
  EXPECT_NEAR(result.integral[0], expected, tolerance);
}

}  // namespace
