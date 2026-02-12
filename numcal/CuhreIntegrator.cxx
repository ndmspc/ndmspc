#include "CuhreIntegrator.h"
#include <cuba.h>

/// \cond CLASSIMP
ClassImp(Ndmspc::CuhreIntegrator);
/// \endcond

namespace Ndmspc {

CuhreIntegrator::CuhreIntegrator(const char* name, const char* title)
    : IntegrationMethod(name, title) {}

CuhreIntegrator::~CuhreIntegrator() {}

nlohmann::json CuhreIntegrator::CuhreOptions::to_json() const {
    return {
        {"ndim", ndim}, {"epsrel", epsrel}, {"epsabs", epsabs},
        {"mineval", mineval}, {"maxeval", maxeval}, {"key", key}, {"verbose", verbose}
    };
}

CuhreIntegrator::CuhreOptions CuhreIntegrator::CuhreOptions::from_json(const nlohmann::json& j) {
    CuhreOptions opts;
    if (j.contains("ndim")) opts.ndim = j["ndim"];
    if (j.contains("epsrel")) opts.epsrel = j["epsrel"];
    if (j.contains("epsabs")) opts.epsabs = j["epsabs"];
    if (j.contains("mineval")) opts.mineval = j["mineval"];
    if (j.contains("maxeval")) opts.maxeval = j["maxeval"];
    if (j.contains("key")) opts.key = j["key"];
    if (j.contains("verbose")) opts.verbose = j["verbose"];
    return opts;
}

int CuhreIntegrator::CuhreIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata) {
    const auto* integrator = static_cast<const CuhreIntegrator*>(userdata);
    if (!integrator) return 1;

    std::vector<double> vars(static_cast<size_t>(*ndim));
    for (int i = 0; i < *ndim; ++i) vars[static_cast<size_t>(i)] = x[i];

    for (int i = 0; i < *ncomp; ++i) {
        f[i] = (i < static_cast<int>(integrator->fFunctions.size())) ?
               integrator->fFunctions[static_cast<size_t>(i)](vars) : 0.0;
    }
    return 0;
}

NumcalResult CuhreIntegrator::Run(const CuhreOptions& options) const {
    NumcalResult result;
    if (fFunctions.empty()) return result;
    // Set method name and title
    result.SetName(GetName());
    result.SetTitle(GetTitle());

    const int ndim = options.ndim;
    const int ncomp = static_cast<int>(fFunctions.size());
    result.IntegralRef().assign(static_cast<size_t>(ncomp), 0.0);
    result.ErrorRef().assign(static_cast<size_t>(ncomp), 0.0);
    result.ProbRef().assign(static_cast<size_t>(ncomp), 0.0);

    int neval = 0, fail = 0, nregions = 0;
    const int nvec = 1, flags = options.verbose;

    Cuhre(ndim, ncomp, CuhreIntegrator::CuhreIntegrand, const_cast<CuhreIntegrator*>(this),
          nvec, options.epsrel, options.epsabs, flags, options.mineval, options.maxeval, options.key,
          nullptr, nullptr, &nregions, &neval, &fail,
          result.IntegralRef().data(), result.ErrorRef().data(), result.ProbRef().data());

    result.SetNeval(neval);
    result.SetFail(fail);
    return result;
}

NumcalResult CuhreIntegrator::RunFromJson(const std::string& json_str) const {
    return RunWithJsonOptions<CuhreOptions>(json_str, CuhreOptions::from_json,
                                           [this](const CuhreOptions& opts) { return Run(opts); });
}

NumcalResult CuhreIntegrator::RunFromFile(const std::string& filename) const {
    return RunWithJsonFile<CuhreOptions>(filename, CuhreOptions::from_json,
                                        [this](const CuhreOptions& opts) { return Run(opts); });
}

NumcalResult CuhreIntegrator::Example() {
    CuhreIntegrator integrator("cuhre-example", "Cuhre example");
    integrator.AddFunction([](const std::vector<double>& x) {
        return (x.size() < 2) ? 0.0 : x[0] * x[1];
    }, "x*y");

    CuhreOptions options;
    options.ndim = 2;
    options.maxeval = 20000;
    return integrator.Run(options);
}

} // namespace Ndmspc