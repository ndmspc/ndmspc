#ifndef Ndmspc_DivonneIntegrator_H
#define Ndmspc_DivonneIntegrator_H

#include "IntegrationMethod.h"

namespace Ndmspc {

class DivonneIntegrator : public IntegrationMethod {
public:
    struct DivonneOptions {
        int ndim = 1;
        double epsrel = 1e-3;
        double epsabs = 1e-12;
        int seed = 0;
        int mineval = 0;
        int maxeval = 50000;
        int key1 = 47;
        int key2 = 1;
        int key3 = 1;
        int maxpass = 5;
        double border = 0.0;
        double maxchisq = 10.0;
        double mindeviation = 0.25;
        int verbose = 0;

        nlohmann::json to_json() const;
        static DivonneOptions from_json(const nlohmann::json& j);
    };

    DivonneIntegrator(const char* name = "divonne", const char* title = "Divonne Integration");
    virtual ~DivonneIntegrator();

    NumcalResult Run() const override { return Run(DivonneOptions{}); }
    NumcalResult RunFromJson(const std::string& json_str) const override;
    NumcalResult RunFromFile(const std::string& filename) const override;
    nlohmann::json GetDefaultOptionsJson() const override { return DivonneOptions{}.to_json(); }

    NumcalResult Run(const DivonneOptions& options) const;
    static NumcalResult Example();

private:
    static int DivonneIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata);
};

} // namespace Ndmspc

#endif