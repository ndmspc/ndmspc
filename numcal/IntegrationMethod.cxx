#include "IntegrationMethod.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::IntegrationMethod);
/// \endcond

namespace Ndmspc {

IntegrationMethod::IntegrationMethod(const char* name, const char* title)
    : TNamed(name, title) {}

IntegrationMethod::~IntegrationMethod() {}

void IntegrationMethod::ClearFunctions() {
    fFunctions.clear();
    fLabels.clear();
}

void IntegrationMethod::AddFunction(const Integrand& fn, const std::string& label) {
    fFunctions.push_back(fn);
    fLabels.push_back(label);
}

void IntegrationMethod::SetFunctions(const std::vector<Integrand>& fns,
                                   const std::vector<std::string>& labels) {
    fFunctions = fns;
    fLabels = labels;
    if (fLabels.size() < fFunctions.size()) {
        fLabels.resize(fFunctions.size());
    }
}

} // namespace Ndmspc