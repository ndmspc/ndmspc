#ifndef Ndmspc_CuhreIntegrator_H
#define Ndmspc_CuhreIntegrator_H

#include "IntegrationMethod.h"

namespace Ndmspc {

class CuhreIntegrator : public IntegrationMethod {
public:
    struct CuhreOptions {
        int ndim = 1;
        double epsrel = 1e-3;
        double epsabs = 1e-12;
        int mineval = 0;
        int maxeval = 50000;
        int key = 0;
        int verbose = 0;

        nlohmann::json to_json() const;
        static CuhreOptions from_json(const nlohmann::json& j);
    };

    CuhreIntegrator(const char* name = "cuhre", const char* title = "Cuhre Integration");
    virtual ~CuhreIntegrator();

    NumcalResult Run() const override { return Run(CuhreOptions{}); }
    NumcalResult RunFromJson(const std::string& json_str) const override;
    NumcalResult RunFromFile(const std::string& filename) const override;
    nlohmann::json GetDefaultOptionsJson() const override { return CuhreOptions{}.to_json(); }

    NumcalResult Run(const CuhreOptions& options) const;
    static NumcalResult Example();

private:
    static int CuhreIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata);
};

} // namespace Ndmspc

#endif