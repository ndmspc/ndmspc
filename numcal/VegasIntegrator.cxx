#include "VegasIntegrator.h"
#include <cuba.h>

/// \cond CLASSIMP
ClassImp(Ndmspc::VegasIntegrator);
/// \endcond

namespace Ndmspc {

VegasIntegrator::VegasIntegrator(const char* name, const char* title)
    : IntegrationMethod(name, title) {}

VegasIntegrator::~VegasIntegrator() {}

nlohmann::json VegasIntegrator::VegasOptions::to_json() const {
    return {
        {"ndim", ndim},
        {"epsrel", epsrel},
        {"epsabs", epsabs},
        {"seed", seed},
        {"mineval", mineval},
        {"maxeval", maxeval},
        {"nstart", nstart},
        {"nincrease", nincrease},
        {"nbatch", nbatch},
        {"gridno", gridno},
        {"verbose", verbose}
    };
}

VegasIntegrator::VegasOptions VegasIntegrator::VegasOptions::from_json(const nlohmann::json& j) {
    VegasOptions opts;
    if (j.contains("ndim")) opts.ndim = j["ndim"];
    if (j.contains("epsrel")) opts.epsrel = j["epsrel"];
    if (j.contains("epsabs")) opts.epsabs = j["epsabs"];
    if (j.contains("seed")) opts.seed = j["seed"];
    if (j.contains("mineval")) opts.mineval = j["mineval"];
    if (j.contains("maxeval")) opts.maxeval = j["maxeval"];
    if (j.contains("nstart")) opts.nstart = j["nstart"];
    if (j.contains("nincrease")) opts.nincrease = j["nincrease"];
    if (j.contains("nbatch")) opts.nbatch = j["nbatch"];
    if (j.contains("gridno")) opts.gridno = j["gridno"];
    if (j.contains("verbose")) opts.verbose = j["verbose"];
    return opts;
}

int VegasIntegrator::VegasIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata) {
    const auto* integrator = static_cast<const VegasIntegrator*>(userdata);
    if (!integrator) {
        return 1;
    }

    std::vector<double> vars(static_cast<size_t>(*ndim));
    for (int i = 0; i < *ndim; ++i) {
        vars[static_cast<size_t>(i)] = x[i];
    }

    for (int i = 0; i < *ncomp; ++i) {
        if (i < static_cast<int>(integrator->fFunctions.size())) {
            f[i] = integrator->fFunctions[static_cast<size_t>(i)](vars);
        } else {
            f[i] = 0.0;
        }
    }
    return 0;
}

NumcalResult VegasIntegrator::Run(const VegasOptions& options) const {
    NumcalResult result;
    if (fFunctions.empty()) {
        return result;
    }
    // Set method name and title
    result.SetName(GetName());
    result.SetTitle(GetTitle());

    const int ndim = options.ndim;
    const int ncomp = static_cast<int>(fFunctions.size());
    result.IntegralRef().assign(static_cast<size_t>(ncomp), 0.0);
    result.ErrorRef().assign(static_cast<size_t>(ncomp), 0.0);
    result.ProbRef().assign(static_cast<size_t>(ncomp), 0.0);

    int neval = 0;
    int fail = 0;
    const int nvec = 1;
    const int flags = options.verbose;

    Vegas(
        ndim,
        ncomp,
        VegasIntegrator::VegasIntegrand,
        const_cast<VegasIntegrator*>(this),
        nvec,
        options.epsrel,
        options.epsabs,
        flags,
        options.seed,
        options.mineval,
        options.maxeval,
        options.nstart,
        options.nincrease,
        options.nbatch,
        options.gridno,
        nullptr,
        nullptr,
        &neval,
        &fail,
        result.IntegralRef().data(),
        result.ErrorRef().data(),
        result.ProbRef().data()
    );

    result.SetNeval(neval);
    result.SetFail(fail);
    return result;
}

NumcalResult VegasIntegrator::RunFromJson(const std::string& json_str) const {
    return RunWithJsonOptions<VegasOptions>(json_str, VegasOptions::from_json,
                                           [this](const VegasOptions& opts) { return Run(opts); });
}

NumcalResult VegasIntegrator::RunFromFile(const std::string& filename) const {
    return RunWithJsonFile<VegasOptions>(filename, VegasOptions::from_json,
                                        [this](const VegasOptions& opts) { return Run(opts); });
}

NumcalResult VegasIntegrator::Example() {
    VegasIntegrator integrator("vegas-example", "Vegas example");
    integrator.AddFunction([](const std::vector<double>& x) {
        if (x.size() < 2) {
            return 0.0;
        }
        return x[0] * x[1];
    }, "x*y");

    VegasOptions options;
    options.ndim = 2;
    options.maxeval = 20000;
    return integrator.Run(options);
}

} // namespace Ndmspc