#include "NNumcalManager.h"

#include <ginac/ginac.h>

void testNumcal()
{
  auto vegas = Ndmspc::NNumcalManager::ExampleVegas();
  if (!vegas.integral.empty()) {
    printf("Vegas example: integral=%g error=%g prob=%g neval=%d fail=%d\n",
           vegas.integral[0],
           vegas.error[0],
           vegas.prob[0],
           vegas.neval,
           vegas.fail);
  }

  auto suave = Ndmspc::NNumcalManager::ExampleSuave();
  if (!suave.integral.empty()) {
    printf("Suave example: integral=%g error=%g prob=%g neval=%d fail=%d\n",
           suave.integral[0],
           suave.error[0],
           suave.prob[0],
           suave.neval,
           suave.fail);
  }

  auto divonne = Ndmspc::NNumcalManager::ExampleDivonne();
  if (!divonne.integral.empty()) {
    printf("Divonne example: integral=%g error=%g prob=%g neval=%d fail=%d\n",
           divonne.integral[0],
           divonne.error[0],
           divonne.prob[0],
           divonne.neval,
           divonne.fail);
  }

  auto cuhre = Ndmspc::NNumcalManager::ExampleCuhre();
  if (!cuhre.integral.empty()) {
    printf("Cuhre example: integral=%g error=%g prob=%g neval=%d fail=%d\n",
           cuhre.integral[0],
           cuhre.error[0],
           cuhre.prob[0],
           cuhre.neval,
           cuhre.fail);
  }

  // GiNaC symbolic example integrated with Vegas
  GiNaC::symbol x("x"), y("y");
  GiNaC::ex expr = GiNaC::sin(x) * y;

  Ndmspc::NNumcalManager mgr("ginac-example", "GiNaC + Vegas");
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
  auto ginac = mgr.RunVegas(options);

  if (!ginac.integral.empty()) {
    printf("GiNaC + Vegas: integral=%g error=%g prob=%g neval=%d fail=%d\n",
           ginac.integral[0],
           ginac.error[0],
           ginac.prob[0],
           ginac.neval,
           ginac.fail);
  }
}
