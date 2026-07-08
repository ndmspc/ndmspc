#include <vector>
#include <limits>
#include <TMath.h>
#include "NLogger.h"

#include "NParameters.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NParameters);
/// \endcond

namespace Ndmspc {
NParameters::NParameters() : TNamed("parameters", "Parameters"), fHisto(nullptr)
{
  ///
  /// Default constructor
  ///
}

NParameters::NParameters(std::vector<std::string> parNames, const char * name, const char * title) : TNamed(name, title)
{
  ///
  /// Constructor
  ///

  if (parNames.empty()) {
    NLogTrace("NParameters::NParameters: No parameter names provided, creating empty parameters histogram.");
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

void NParameters::Print(Option_t * /*option*/) const
{
  ///
  /// Print parameters
  ///
  if (fHisto == nullptr) {
    NLogError("NParameters::Print: Parameters histogram is not initialized !!!");
    return;
  }

  // fHisto->Print("all");

  // loop over all bins and print the parameter name, value and error
  const int     nbins = fHisto->GetNbinsX();
  const TAxis * xaxis = fHisto->GetXaxis();
  for (int bin = 1; bin <= nbins; bin++) {
    std::string parName = xaxis->GetBinLabel(bin);
    double      value   = fHisto->GetBinContent(bin);
    double      error   = fHisto->GetBinError(bin);
    NLogInfo("Parameter '%s': %e +/- %e", parName.c_str(), value, error);
  }
}

bool NParameters::SetParameter(int bin, Double_t value, Double_t error)
{
  ///
  /// Set parameter by index
  ///
  if (fHisto == nullptr) {
    NLogError("NParameters::SetParameter: Parameters histogram is not initialized !!!");
    return false;
  }

  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    return false;
  }

  // if values or error is less then eps, set them to epsion to avoid zero values
  const double eps = std::numeric_limits<double>::epsilon();
  if (std::abs(value) < eps) {
    value = eps;
  }
  if (std::abs(error) < eps) {
    error = eps;
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
  if (fHisto == nullptr) {
    NLogError("NParameters::SetParameter: Parameters histogram is not initialized !!!");
    return false;
  }

  int bin = fHisto->GetXaxis()->FindBin(parName);
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::SetParameter: Parameter name '%s' not found !!!", parName);
    return false;
  }

  // if values or error is less then eps, set them to epsion to avoid zero values
  const double eps = std::numeric_limits<double>::epsilon();
  if (std::abs(value) < eps) {
    value = eps;
  }
  if (std::abs(error) < eps) {
    error = eps;
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
  if (fHisto == nullptr) {
    NLogError("NParameters::GetParameter: Parameters histogram is not initialized !!!");
    return TMath::QuietNaN();
  }

  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParameter: Parameter index '%d' out of range !!!", bin);
    return TMath::QuietNaN();
  }
  return fHisto->GetBinContent(bin);
}

Double_t NParameters::GetParameter(const char * parName) const
{
  ///
  /// Get parameter by name
  ///
  if (fHisto == nullptr) {
    NLogError("NParameters::GetParameter: Parameters histogram is not initialized !!!");
    return TMath::QuietNaN();
  }

  int bin = fHisto->GetXaxis()->FindBin(parName);
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParameter: Parameter name '%s' not found !!!", parName);
    return TMath::QuietNaN();
  }
  return fHisto->GetBinContent(bin);
}
Double_t NParameters::GetParameterError(int bin) const
{
  ///
  /// Get parameter error by index
  ///
  if (fHisto == nullptr) {
    NLogError("NParameters::GetParError: Parameters histogram is not initialized !!!");
    return TMath::QuietNaN();
  }

  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParError: Parameter index '%d' out of range !!!", bin);
    return TMath::QuietNaN();
  }
  return fHisto->GetBinError(bin);
}
Double_t NParameters::GetParameterError(const char * parName) const
{
  ///
  /// Get parameter error by name
  ///
  if (fHisto == nullptr) {
    NLogError("NParameters::GetParError: Parameters histogram is not initialized !!!");
    return TMath::QuietNaN();
  }

  int bin = fHisto->GetXaxis()->FindBin(parName);
  if (bin < 1 || bin > fHisto->GetNbinsX()) {
    NLogError("NParameters::GetParError: Parameter name '%s' not found !!!", parName);
    return TMath::QuietNaN();
  }
  return fHisto->GetBinError(bin);
}

std::vector<std::string> NParameters::GetNames() const
{
  ///
  /// Get parameter names
  ///
  if (fHisto == nullptr) {
    NLogError("NParameters::GetNames: Parameters histogram is not initialized !!!");
    return {};
  }

  const int                nbins = fHisto->GetNbinsX();
  const TAxis *            xaxis = fHisto->GetXaxis();
  std::vector<std::string> names;
  names.reserve(nbins);
  for (int bin = 1; bin <= nbins; bin++) {
    names.emplace_back(xaxis->GetBinLabel(bin));
  }
  return names;
}
} // namespace Ndmspc
