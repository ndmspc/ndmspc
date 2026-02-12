#include "NNumcalManager.h"
 #include "NumcalResult.h"


 #include <cuba.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <nlohmann/json.hpp>

/// \cond CLASSIMP
ClassImp(Ndmspc::NNumcalManager);
/// \endcond

namespace Ndmspc {
void NNumcalManager::Print(Option_t* option) const {
	(void)option; // Suppress unused parameter warning
       NLogDebug("NNumcalManager: %s (%s)", GetName(), GetTitle());
       NLogDebug("Imported functions:");
	int idx = 1;
	for (const auto& label : fLabels) {
        NLogDebug("  f%d %s", idx++, label.c_str());
	}
	if (fLabels.empty()) {
           NLogDebug("  (none)");
	}
}
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

// High-level interface implementations
NumcalResult NNumcalManager::RunVegas() const {
    VegasIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run();
}

NumcalResult NNumcalManager::RunSuave() const {
    SuaveIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run();
}

NumcalResult NNumcalManager::RunDivonne() const {
    DivonneIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run();
}

NumcalResult NNumcalManager::RunCuhre() const {
    CuhreIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run();
}

// JSON-based methods
NumcalResult NNumcalManager::RunVegasFromJson(const std::string& json_str) const {
    VegasIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromJson(json_str);
}

NumcalResult NNumcalManager::RunVegasFromFile(const std::string& filename) const {
    VegasIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromFile(filename);
}

NumcalResult NNumcalManager::RunSuaveFromJson(const std::string& json_str) const {
    SuaveIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromJson(json_str);
}

NumcalResult NNumcalManager::RunSuaveFromFile(const std::string& filename) const {
    SuaveIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromFile(filename);
}

NumcalResult NNumcalManager::RunDivonneFromJson(const std::string& json_str) const {
    DivonneIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromJson(json_str);
}

NumcalResult NNumcalManager::RunDivonneFromFile(const std::string& filename) const {
    DivonneIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromFile(filename);
}

NumcalResult NNumcalManager::RunCuhreFromJson(const std::string& json_str) const {
    CuhreIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromJson(json_str);
}

NumcalResult NNumcalManager::RunCuhreFromFile(const std::string& filename) const {
    CuhreIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.RunFromFile(filename);
}

// Backward compatibility methods with explicit options
NumcalResult NNumcalManager::RunVegas(const VegasIntegrator::VegasOptions& options) const {
    VegasIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run(options);
}

NumcalResult NNumcalManager::RunSuave(const SuaveIntegrator::SuaveOptions& options) const {
    SuaveIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run(options);
}

NumcalResult NNumcalManager::RunDivonne(const DivonneIntegrator::DivonneOptions& options) const {
    DivonneIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run(options);
}

NumcalResult NNumcalManager::RunCuhre(const CuhreIntegrator::CuhreOptions& options) const {
    CuhreIntegrator integrator;
    integrator.SetFunctions(fFunctions, fLabels);
    return integrator.Run(options);
}

// Static example methods
NumcalResult NNumcalManager::ExampleVegas() {
    return VegasIntegrator::Example();
}

NumcalResult NNumcalManager::ExampleSuave() {
    return SuaveIntegrator::Example();
}

NumcalResult NNumcalManager::ExampleDivonne() {
    return DivonneIntegrator::Example();
}

NumcalResult NNumcalManager::ExampleCuhre() {
    return CuhreIntegrator::Example();
}

// Default options access
nlohmann::json NNumcalManager::GetDefaultVegasOptions() {
    return VegasIntegrator::VegasOptions{}.to_json();
}

nlohmann::json NNumcalManager::GetDefaultSuaveOptions() {
    return SuaveIntegrator::SuaveOptions{}.to_json();
}

nlohmann::json NNumcalManager::GetDefaultDivonneOptions() {
    return DivonneIntegrator::DivonneOptions{}.to_json();
}

nlohmann::json NNumcalManager::GetDefaultCuhreOptions() {
    return CuhreIntegrator::CuhreOptions{}.to_json();
}

} // namespace Ndmspc
