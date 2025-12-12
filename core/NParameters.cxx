#include <vector>
#include "NLogger.h"
#include "RtypesCore.h"
#include "NParameters.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NParameters);
/// \endcond

namespace Ndmspc {
NParameters::NParameters(const char * name, const char * title, std::vector<std::string> parNames) : TNamed(name, title)
{
  ///
  /// Constructor
  ///

  if (parNames.empty()) {
    NLogWarning("NParameters::NParameters: No parameter names provided, creating empty parameters histogram.");
    return;
  }
  fHisto = new TH1D("ParametersHisto", "ParametersHisto", parNames.size(), 0, parNames.size());
  // set parameter names as labels
  for (size_t i = 0; i < parNames.size(); i++) {
    fHisto->GetXaxis()->SetBinLabel(i + 1, parNames[i].c_str());
  }

  fHisto->Sumw2(kFALSE); // Disable sum of squares of weights for error calculation
}
NParameters::~NParameters()
{
  ///
  /// Destructor
  ///
  delete fHisto;
}

void NParameters::Print(Option_t * option) const
{
  ///
  /// Print parameters
  ///
  fHisto->Print("all");
}

bool NParameters::SetParameter(int bin, Double_t value, Double_t error)
{
  ///
  /// Set parameter by index
  ///
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    return false;
  }
  fHisto->SetBinContent(bin, value);
  fHisto->SetBinError(bin, error);
  return true;
}

bool NParameters::SetParameter(const char * parName, Double_t value, Double_t error)
{
  ///
  /// Set parameter by name
  ///
  int bin = fHisto->GetXaxis()->FindBin(parName);
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::SetParameter: Parameter name '%s' not found !!!", parName);
    return false;
  }
  fHisto->SetBinContent(bin, value);
  fHisto->SetBinError(bin, error);
  return true;
}

Double_t NParameters::GetParameter(int bin) const
{
  ///
  /// Get parameter by index
  ///
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParameter: Parameter index '%d' out of range !!!", bin);
    return NAN;
  }
  return fHisto->GetBinContent(bin);
}

Double_t NParameters::GetParameter(const char * parName) const
{
  ///
  /// Get parameter by name
  ///
  int bin = fHisto->GetXaxis()->FindBin(parName);
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParameter: Parameter name '%s' not found !!!", parName);
    return NAN;
  }
  return fHisto->GetBinContent(bin);
}
Double_t NParameters::GetParError(int bin) const
{
  ///
  /// Get parameter error by index
  ///
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParError: Parameter index '%d' out of range !!!", bin);
    return NAN;
  }
  return fHisto->GetBinError(bin);
}
Double_t NParameters::GetParError(const char * parName) const
{
  ///
  /// Get parameter error by name
  ///
  int bin = fHisto->GetXaxis()->FindBin(parName);
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParError: Parameter name '%s' not found !!!", parName);
    return NAN;
  }
  return fHisto->GetBinError(bin);
}

} // namespace Ndmspc
