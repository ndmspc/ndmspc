#ifndef Ndmspc_IntegrationMethod_H
#define Ndmspc_IntegrationMethod_H

#include <TNamed.h>
#include <vector>
#include <functional>
#include <fstream>
#include <nlohmann/json.hpp>

#include "NumcalResult.h"
#include "NLogger.h"

namespace Ndmspc {

class IntegrationMethod : public TNamed {
public:
    using Integrand = std::function<double(const std::vector<double>&)>;

    IntegrationMethod(const char* name = "", const char* title = "");
    virtual ~IntegrationMethod();

    // Function management
    void ClearFunctions();
    void AddFunction(const Integrand& fn, const std::string& label = "");
    void SetFunctions(const std::vector<Integrand>& fns, const std::vector<std::string>& labels = {});
    const std::vector<std::string>& GetLabels() const { return fLabels; }
    const std::vector<Integrand>& GetFunctions() const { return fFunctions; }

    // Pure virtual methods to be implemented by derived classes
    virtual NumcalResult Run() const = 0;
    virtual NumcalResult RunFromJson(const std::string& json_str) const = 0;
    virtual NumcalResult RunFromFile(const std::string& filename) const = 0;
    virtual nlohmann::json GetDefaultOptionsJson() const = 0;

protected:
    std::vector<Integrand> fFunctions;
    std::vector<std::string> fLabels;

    // Helper method for JSON parsing with error handling
    template<typename OptionsType>
    NumcalResult RunWithJsonOptions(const std::string& json_str,
                                   std::function<OptionsType(const nlohmann::json&)> from_json,
                                   std::function<NumcalResult(const OptionsType&)> runner) const;

    template<typename OptionsType>
    NumcalResult RunWithJsonFile(const std::string& filename,
                                std::function<OptionsType(const nlohmann::json&)> from_json,
                                std::function<NumcalResult(const OptionsType&)> runner) const;
};

template<typename OptionsType>
NumcalResult IntegrationMethod::RunWithJsonOptions(
    const std::string& json_str,
    std::function<OptionsType(const nlohmann::json&)> from_json,
    std::function<NumcalResult(const OptionsType&)> runner) const {

    try {
        nlohmann::json j = nlohmann::json::parse(json_str);
        OptionsType opts = from_json(j);
        return runner(opts);
    } catch (const std::exception& e) {
        NLogError("Error parsing JSON for integration options: %s", e.what());
        return NumcalResult();
    }
}

template<typename OptionsType>
NumcalResult IntegrationMethod::RunWithJsonFile(
    const std::string& filename,
    std::function<OptionsType(const nlohmann::json&)> from_json,
    std::function<NumcalResult(const OptionsType&)> runner) const {

    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            NLogError("Error opening file: %s", filename.c_str());
            return NumcalResult();
        }
        nlohmann::json j;
        file >> j;
        OptionsType opts = from_json(j);
        return runner(opts);
    } catch (const std::exception& e) {
        NLogError("Error reading/parsing JSON file for integration options: %s", e.what());
        return NumcalResult();
    }
}

} // namespace Ndmspc

#endif