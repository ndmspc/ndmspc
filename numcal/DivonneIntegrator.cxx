#include "DivonneIntegrator.h"
#include <cuba.h>

/// \cond CLASSIMP
ClassImp(Ndmspc::DivonneIntegrator);
/// \endcond

namespace Ndmspc {

DivonneIntegrator::DivonneIntegrator(const char* name, const char* title)
    : IntegrationMethod(name, title) {}

DivonneIntegrator::~DivonneIntegrator() {}

nlohmann::json DivonneIntegrator::DivonneOptions::to_json() const {
    return {
        {"ndim", ndim}, {"epsrel", epsrel}, {"epsabs", epsabs}, {"seed", seed},
        {"mineval", mineval}, {"maxeval", maxeval}, {"key1", key1}, {"key2", key2},
        {"key3", key3}, {"maxpass", maxpass}, {"border", border},
        {"maxchisq", maxchisq}, {"mindeviation", mindeviation}, {"verbose", verbose}
    };
}

DivonneIntegrator::DivonneOptions DivonneIntegrator::DivonneOptions::from_json(const nlohmann::json& j) {
    DivonneOptions opts;
    if (j.contains("ndim")) opts.ndim = j["ndim"];
    if (j.contains("epsrel")) opts.epsrel = j["epsrel"];
    if (j.contains("epsabs")) opts.epsabs = j["epsabs"];
    if (j.contains("seed")) opts.seed = j["seed"];
    if (j.contains("mineval")) opts.mineval = j["mineval"];
    if (j.contains("maxeval")) opts.maxeval = j["maxeval"];
    if (j.contains("key1")) opts.key1 = j["key1"];
    if (j.contains("key2")) opts.key2 = j["key2"];
    if (j.contains("key3")) opts.key3 = j["key3"];
    if (j.contains("maxpass")) opts.maxpass = j["maxpass"];
    if (j.contains("border")) opts.border = j["border"];
    if (j.contains("maxchisq")) opts.maxchisq = j["maxchisq"];
    if (j.contains("mindeviation")) opts.mindeviation = j["mindeviation"];
    if (j.contains("verbose")) opts.verbose = j["verbose"];
    return opts;
}

int DivonneIntegrator::DivonneIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata) {
    const auto* integrator = static_cast<const DivonneIntegrator*>(userdata);
    if (!integrator) return 1;

    std::vector<double> vars(static_cast<size_t>(*ndim));
    for (int i = 0; i < *ndim; ++i) vars[static_cast<size_t>(i)] = x[i];

    for (int i = 0; i < *ncomp; ++i) {
        f[i] = (i < static_cast<int>(integrator->fFunctions.size())) ?
               integrator->fFunctions[static_cast<size_t>(i)](vars) : 0.0;
    }
    return 0;
}

NumcalResult DivonneIntegrator::Run(const DivonneOptions& options) const {
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
    const int nvec = 1, flags = options.verbose, ngiven = 0, ldxgiven = 0, nextra = 0;
    cubareal* xgiven = nullptr;
    const peakfinder_t peakfinder = nullptr;

    Divonne(ndim, ncomp, DivonneIntegrator::DivonneIntegrand, const_cast<DivonneIntegrator*>(this),
            nvec, options.epsrel, options.epsabs, flags, options.seed, options.mineval, options.maxeval,
            options.key1, options.key2, options.key3, options.maxpass, options.border, options.maxchisq,
            options.mindeviation, ngiven, ldxgiven, xgiven, nextra, peakfinder,
            nullptr, nullptr, &nregions, &neval, &fail,
            result.IntegralRef().data(), result.ErrorRef().data(), result.ProbRef().data());

    result.SetNeval(neval);
    result.SetFail(fail);
    return result;
}

NumcalResult DivonneIntegrator::RunFromJson(const std::string& json_str) const {
    return RunWithJsonOptions<DivonneOptions>(json_str, DivonneOptions::from_json,
                                             [this](const DivonneOptions& opts) { return Run(opts); });
}

NumcalResult DivonneIntegrator::RunFromFile(const std::string& filename) const {
    return RunWithJsonFile<DivonneOptions>(filename, DivonneOptions::from_json,
                                          [this](const DivonneOptions& opts) { return Run(opts); });
}

NumcalResult DivonneIntegrator::Example() {
    DivonneIntegrator integrator("divonne-example", "Divonne example");
    integrator.AddFunction([](const std::vector<double>& x) {
        return (x.size() < 2) ? 0.0 : x[0] * x[1];
    }, "x*y");

    DivonneOptions options;
    options.ndim = 2;
    options.maxeval = 20000;
    return integrator.Run(options);
}

} // namespace Ndmspc