#ifndef Ndmspc_VegasIntegrator_H
#define Ndmspc_VegasIntegrator_H

#include "IntegrationMethod.h"

namespace Ndmspc {

class VegasIntegrator : public IntegrationMethod {
public:
    struct VegasOptions {
        int ndim = 1;
        double epsrel = 1e-3;
        double epsabs = 1e-12;
        int seed = 0;
        int mineval = 0;
        int maxeval = 50000;
        int nstart = 1000;
        int nincrease = 500;
        int nbatch = 1000;
        int gridno = 0;
        int verbose = 0;

        nlohmann::json to_json() const;
        static VegasOptions from_json(const nlohmann::json& j);
    };

    VegasIntegrator(const char* name = "vegas", const char* title = "Vegas Integration");
    virtual ~VegasIntegrator();

    // Implementation of pure virtual methods
    NumcalResult Run() const override { return Run(VegasOptions{}); }
    NumcalResult RunFromJson(const std::string& json_str) const override;
    NumcalResult RunFromFile(const std::string& filename) const override;
    nlohmann::json GetDefaultOptionsJson() const override { return VegasOptions{}.to_json(); }

    // Method with explicit options
    NumcalResult Run(const VegasOptions& options) const;

    // Static example method
    static NumcalResult Example();

private:
    static int VegasIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata);
};

} // namespace Ndmspc

#endif