#ifndef Ndmspc_NNumcalManager_H
#define Ndmspc_NNumcalManager_H
#include <functional>
#include <string>
#include <vector>
#include <TNamed.h>
#include <nlohmann/json.hpp>

#include "NumcalResult.h"
#include "VegasIntegrator.h"
#include "SuaveIntegrator.h"
#include "DivonneIntegrator.h"
#include "CuhreIntegrator.h"

namespace Ndmspc {

///
/// \class NNumcalManager
///
/// \brief NNumcalManager object - High-level interface for numerical integration
///	\author Martin Vala <mvala@cern.ch>
///

class NNumcalManager : public TNamed {
public:
    using Integrand = std::function<double(const std::vector<double>&)>;
    NNumcalManager(const char *name = "", const char *title = "");
    virtual ~NNumcalManager();
    void Print(Option_t* option = "") const override;
    const std::vector<std::string>& GetLabels() const { return fLabels; }
    const std::vector<Integrand>& GetFunctions() const { return fFunctions; }
    void ClearFunctions();
    void AddFunction(const Integrand& fn, const std::string& label = "");
    void SetFunctions(const std::vector<Integrand>& fns, const std::vector<std::string>& labels = {});

    // High-level interface methods with default parameters
    NumcalResult RunVegas() const;
    NumcalResult RunSuave() const;
    NumcalResult RunDivonne() const;
    NumcalResult RunCuhre() const;

    // JSON configuration methods
    NumcalResult RunVegasFromJson(const std::string& json_str) const;
    NumcalResult RunVegasFromFile(const std::string& filename) const;
    NumcalResult RunSuaveFromJson(const std::string& json_str) const;
    NumcalResult RunSuaveFromFile(const std::string& filename) const;
    NumcalResult RunDivonneFromJson(const std::string& json_str) const;
    NumcalResult RunDivonneFromFile(const std::string& filename) const;
    NumcalResult RunCuhreFromJson(const std::string& json_str) const;
    NumcalResult RunCuhreFromFile(const std::string& filename) const;

    // Methods with explicit options (for backward compatibility)
    NumcalResult RunVegas(const VegasIntegrator::VegasOptions& options) const;
    NumcalResult RunSuave(const SuaveIntegrator::SuaveOptions& options) const;
    NumcalResult RunDivonne(const DivonneIntegrator::DivonneOptions& options) const;
    NumcalResult RunCuhre(const CuhreIntegrator::CuhreOptions& options) const;

    // Static example methods
    static NumcalResult ExampleVegas();
    static NumcalResult ExampleSuave();
    static NumcalResult ExampleDivonne();
    static NumcalResult ExampleCuhre();

    // Access to default options as JSON
    static nlohmann::json GetDefaultVegasOptions();
    static nlohmann::json GetDefaultSuaveOptions();
    static nlohmann::json GetDefaultDivonneOptions();
    static nlohmann::json GetDefaultCuhreOptions();

private:
    std::vector<Integrand> fFunctions; //!
    std::vector<std::string> fLabels;  //!

    /// \cond CLASSIMP
    ClassDefOverride(NNumcalManager, 1);
    /// \endcond
};

} // namespace Ndmspc

#endif
