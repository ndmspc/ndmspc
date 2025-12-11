#include <vector>
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
  fHisto = new TH1D("ParametersHisto", "ParametersHisto", parNames.size(), 0, parNames.size());
  // set parameter names as labels
  for (size_t i = 0; i < parNames.size(); i++) {
    fHisto->GetXaxis()->SetBinLabel(i + 1, parNames[i].c_str());
  }
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
  return fHisto->GetBinContent(bin);
}

Double_t NParameters::GetParameter(const char * parName) const
{
  ///
  /// Get parameter by name
  ///
  int bin = fHisto->GetXaxis()->FindBin(parName);
  return fHisto->GetBinContent(bin);
}
Double_t NParameters::GetParError(int bin) const
{
  ///
  /// Get parameter error by index
  ///
  return fHisto->GetBinError(bin);
}
Double_t NParameters::GetParError(const char * parName) const
{
  ///
  /// Get parameter error by name
  ///
  int bin = fHisto->GetXaxis()->FindBin(parName);
  return fHisto->GetBinError(bin);
}

} // namespace Ndmspc
