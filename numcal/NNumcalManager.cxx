#include "NNumcalManager.h"

#include <cuba.h>

/// \cond CLASSIMP
ClassImp(Ndmspc::NNumcalManager);
/// \endcond

namespace Ndmspc
{
NNumcalManager::NNumcalManager(const char *name, const char* title) : TNamed(name,title) {}
NNumcalManager::~NNumcalManager() {}

void NNumcalManager::ClearFunctions()
{
	fFunctions.clear();
	fLabels.clear();
}

void NNumcalManager::AddFunction(const Integrand& fn, const std::string& label)
{
	fFunctions.push_back(fn);
	fLabels.push_back(label);
}

void NNumcalManager::SetFunctions(const std::vector<Integrand>& fns, const std::vector<std::string>& labels)
{
	fFunctions = fns;
	fLabels = labels;
	if (fLabels.size() < fFunctions.size()) {
		fLabels.resize(fFunctions.size());
	}
}

int NNumcalManager::VegasIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata)
{
	const auto* mgr = static_cast<const NNumcalManager*>(userdata);
	if (!mgr) {
		return 1;
	}

	std::vector<double> vars(static_cast<size_t>(*ndim));
	for (int i = 0; i < *ndim; ++i) {
		vars[static_cast<size_t>(i)] = x[i];
	}

	for (int i = 0; i < *ncomp; ++i) {
		if (i < static_cast<int>(mgr->fFunctions.size())) {
			f[i] = mgr->fFunctions[static_cast<size_t>(i)](vars);
		} else {
			f[i] = 0.0;
		}
	}
	return 0;
}

NNumcalManager::VegasResult NNumcalManager::RunVegas(const VegasOptions& options) const
{
	VegasResult result;
	if (fFunctions.empty()) {
		return result;
	}

	const int ndim = options.ndim;
	const int ncomp = static_cast<int>(fFunctions.size());
	result.integral.assign(static_cast<size_t>(ncomp), 0.0);
	result.error.assign(static_cast<size_t>(ncomp), 0.0);
	result.prob.assign(static_cast<size_t>(ncomp), 0.0);

	int neval = 0;
	int fail = 0;
	const int nvec = 1;
	const int flags = options.verbose;

	Vegas(
		ndim,
		ncomp,
		NNumcalManager::VegasIntegrand,
		const_cast<NNumcalManager*>(this),
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
		result.integral.data(),
		result.error.data(),
		result.prob.data()
	);

	result.neval = neval;
	result.fail = fail;
	return result;
}

NNumcalManager::VegasResult NNumcalManager::RunSuave(const SuaveOptions& options) const
{
	VegasResult result;
	if (fFunctions.empty()) {
		return result;
	}

	const int ndim = options.ndim;
	const int ncomp = static_cast<int>(fFunctions.size());
	result.integral.assign(static_cast<size_t>(ncomp), 0.0);
	result.error.assign(static_cast<size_t>(ncomp), 0.0);
	result.prob.assign(static_cast<size_t>(ncomp), 0.0);

	int neval = 0;
	int fail = 0;
	int nregions = 0;
	const int nvec = 1;
	const int flags = options.verbose;

	Suave(
		ndim,
		ncomp,
		NNumcalManager::VegasIntegrand,
		const_cast<NNumcalManager*>(this),
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
		result.integral.data(),
		result.error.data(),
		result.prob.data()
	);

	result.neval = neval;
	result.fail = fail;
	return result;
}

NNumcalManager::VegasResult NNumcalManager::RunDivonne(const DivonneOptions& options) const
{
	VegasResult result;
	if (fFunctions.empty()) {
		return result;
	}

	const int ndim = options.ndim;
	const int ncomp = static_cast<int>(fFunctions.size());
	result.integral.assign(static_cast<size_t>(ncomp), 0.0);
	result.error.assign(static_cast<size_t>(ncomp), 0.0);
	result.prob.assign(static_cast<size_t>(ncomp), 0.0);

	int neval = 0;
	int fail = 0;
	int nregions = 0;
	const int nvec = 1;
	const int flags = options.verbose;
	const int ngiven = 0;
	const int ldxgiven = 0;
	cubareal* xgiven = nullptr;
	const int nextra = 0;
	const peakfinder_t peakfinder = nullptr;

	Divonne(
		ndim,
		ncomp,
		NNumcalManager::VegasIntegrand,
		const_cast<NNumcalManager*>(this),
		nvec,
		options.epsrel,
		options.epsabs,
		flags,
		options.seed,
		options.mineval,
		options.maxeval,
		options.key1,
		options.key2,
		options.key3,
		options.maxpass,
		options.border,
		options.maxchisq,
		options.mindeviation,
		ngiven,
		ldxgiven,
		xgiven,
		nextra,
		peakfinder,
		nullptr,
		nullptr,
		&nregions,
		&neval,
		&fail,
		result.integral.data(),
		result.error.data(),
		result.prob.data()
	);

	result.neval = neval;
	result.fail = fail;
	return result;
}

NNumcalManager::VegasResult NNumcalManager::RunCuhre(const CuhreOptions& options) const
{
	VegasResult result;
	if (fFunctions.empty()) {
		return result;
	}

	const int ndim = options.ndim;
	const int ncomp = static_cast<int>(fFunctions.size());
	result.integral.assign(static_cast<size_t>(ncomp), 0.0);
	result.error.assign(static_cast<size_t>(ncomp), 0.0);
	result.prob.assign(static_cast<size_t>(ncomp), 0.0);

	int neval = 0;
	int fail = 0;
	int nregions = 0;
	const int nvec = 1;
	const int flags = options.verbose;

	Cuhre(
		ndim,
		ncomp,
		NNumcalManager::VegasIntegrand,
		const_cast<NNumcalManager*>(this),
		nvec,
		options.epsrel,
		options.epsabs,
		flags,
		options.mineval,
		options.maxeval,
		options.key,
		nullptr,
		nullptr,
		&nregions,
		&neval,
		&fail,
		result.integral.data(),
		result.error.data(),
		result.prob.data()
	);

	result.neval = neval;
	result.fail = fail;
	return result;
}

NNumcalManager::VegasResult NNumcalManager::ExampleVegas()
{
	NNumcalManager mgr("vegas-example", "Vegas example");
	mgr.AddFunction([](const std::vector<double>& x) {
		if (x.size() < 2) {
			return 0.0;
		}
		return x[0] * x[1];
	}, "x*y");

	VegasOptions options;
	options.ndim = 2;
	options.maxeval = 20000;
	return mgr.RunVegas(options);
}

NNumcalManager::VegasResult NNumcalManager::ExampleSuave()
{
	NNumcalManager mgr("suave-example", "Suave example");
	mgr.AddFunction([](const std::vector<double>& x) {
		if (x.size() < 2) {
			return 0.0;
		}
		return x[0] * x[1];
	}, "x*y");

	SuaveOptions options;
	options.ndim = 2;
	options.maxeval = 20000;
	return mgr.RunSuave(options);
}

NNumcalManager::VegasResult NNumcalManager::ExampleDivonne()
{
	NNumcalManager mgr("divonne-example", "Divonne example");
	mgr.AddFunction([](const std::vector<double>& x) {
		if (x.size() < 2) {
			return 0.0;
		}
		return x[0] * x[1];
	}, "x*y");

	DivonneOptions options;
	options.ndim = 2;
	options.maxeval = 20000;
	return mgr.RunDivonne(options);
}

NNumcalManager::VegasResult NNumcalManager::ExampleCuhre()
{
	NNumcalManager mgr("cuhre-example", "Cuhre example");
	mgr.AddFunction([](const std::vector<double>& x) {
		if (x.size() < 2) {
			return 0.0;
		}
		return x[0] * x[1];
	}, "x*y");

	CuhreOptions options;
	options.ndim = 2;
	options.maxeval = 20000;
	return mgr.RunCuhre(options);
}
} // namespace Ndmspc
