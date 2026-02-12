#include "SuaveIntegrator.h"
#include <cuba.h>

/// \cond CLASSIMP
ClassImp(Ndmspc::SuaveIntegrator);
/// \endcond

namespace Ndmspc {

SuaveIntegrator::SuaveIntegrator(const char* name, const char* title)
    : IntegrationMethod(name, title) {}

SuaveIntegrator::~SuaveIntegrator() {}

nlohmann::json SuaveIntegrator::SuaveOptions::to_json() const {
    return {
        {"ndim", ndim},
        {"epsrel", epsrel},
        {"epsabs", epsabs},
        {"seed", seed},
        {"mineval", mineval},
        {"maxeval", maxeval},
        {"nnew", nnew},
        {"nmin", nmin},
        {"flatness", flatness},
        {"verbose", verbose}
    };
}

SuaveIntegrator::SuaveOptions SuaveIntegrator::SuaveOptions::from_json(const nlohmann::json& j) {
    SuaveOptions opts;
    if (j.contains("ndim")) opts.ndim = j["ndim"];
    if (j.contains("epsrel")) opts.epsrel = j["epsrel"];
    if (j.contains("epsabs")) opts.epsabs = j["epsabs"];
    if (j.contains("seed")) opts.seed = j["seed"];
    if (j.contains("mineval")) opts.mineval = j["mineval"];
    if (j.contains("maxeval")) opts.maxeval = j["maxeval"];
    if (j.contains("nnew")) opts.nnew = j["nnew"];
    if (j.contains("nmin")) opts.nmin = j["nmin"];
    if (j.contains("flatness")) opts.flatness = j["flatness"];
    if (j.contains("verbose")) opts.verbose = j["verbose"];
    return opts;
}

int SuaveIntegrator::SuaveIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata) {
    const auto* integrator = static_cast<const SuaveIntegrator*>(userdata);
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

NumcalResult SuaveIntegrator::Run(const SuaveOptions& options) const {
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
    int nregions = 0;
    const int nvec = 1;
    const int flags = options.verbose;

    Suave(
        ndim,
        ncomp,
        SuaveIntegrator::SuaveIntegrand,
        const_cast<SuaveIntegrator*>(this),
        nvec,
        options.epsrel,
        options.epsabs,
        flags,
        options.seed,
        options.mineval,
        options.maxeval,
        options.nnew,
        options.nmin,
        options.flatness,
        nullptr,
        nullptr,
        &nregions,
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

NumcalResult SuaveIntegrator::RunFromJson(const std::string& json_str) const {
    return RunWithJsonOptions<SuaveOptions>(json_str, SuaveOptions::from_json,
                                           [this](const SuaveOptions& opts) { return Run(opts); });
}

NumcalResult SuaveIntegrator::RunFromFile(const std::string& filename) const {
    return RunWithJsonFile<SuaveOptions>(filename, SuaveOptions::from_json,
                                        [this](const SuaveOptions& opts) { return Run(opts); });
}

NumcalResult SuaveIntegrator::Example() {
    SuaveIntegrator integrator("suave-example", "Suave example");
    integrator.AddFunction([](const std::vector<double>& x) {
        if (x.size() < 2) {
            return 0.0;
        }
        return x[0] * x[1];
    }, "x*y");

    SuaveOptions options;
    options.ndim = 2;
    options.maxeval = 20000;
    return integrator.Run(options);
}

} // namespace Ndmspc