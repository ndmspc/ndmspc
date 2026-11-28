#ifndef Ndmspc_SuaveIntegrator_H
#define Ndmspc_SuaveIntegrator_H

#include "IntegrationMethod.h"

namespace Ndmspc {

class SuaveIntegrator : public IntegrationMethod {
public:
    struct SuaveOptions {
        int ndim = 1;
        double epsrel = 1e-3;
        double epsabs = 1e-12;
        int seed = 0;
        int mineval = 0;
        int maxeval = 50000;
        int nnew = 1000;
        int nmin = 2;
        double flatness = 25.0;
        int verbose = 0;

        nlohmann::json to_json() const;
        static SuaveOptions from_json(const nlohmann::json& j);
    };

    SuaveIntegrator(const char* name = "suave", const char* title = "Suave Integration");
    virtual ~SuaveIntegrator();

    // Implementation of pure virtual methods
    NumcalResult Run() const override { return Run(SuaveOptions{}); }
    NumcalResult RunFromJson(const std::string& json_str) const override;
    NumcalResult RunFromFile(const std::string& filename) const override;
    nlohmann::json GetDefaultOptionsJson() const override { return SuaveOptions{}.to_json(); }

    // Method with explicit options
    NumcalResult Run(const SuaveOptions& options) const;

    // Static example method
    static NumcalResult Example();

private:
    static int SuaveIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata);
};

} // namespace Ndmspc

#endif