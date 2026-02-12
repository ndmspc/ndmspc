#include "NumcalResult.h"
#include "NLogger.h"
#include <iostream>

namespace Ndmspc {
NumcalResult::NumcalResult() : TNamed("", "") {}
NumcalResult::NumcalResult(const char* name, const char* title) : TNamed(name, title) {}
NumcalResult::~NumcalResult() {}

void NumcalResult::Print(Option_t* option) const {
    (void)option; // Suppress unused parameter warning
    for (size_t i = 0; i < fIntegral.size(); ++i) {
        NLogInfo("%s f%d: %e err=%e prob=%e", GetName(), (int)(i+1),
            fIntegral[i],
            (i < fError.size() ? fError[i] : 0.0),
            (i < fProb.size() ? fProb[i] : 0.0));
    }
}

} // namespace Ndmspc
