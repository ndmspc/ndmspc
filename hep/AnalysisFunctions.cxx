#include "AnalysisFunctions.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::AnalysisFunctions);
/// \endcond

namespace Ndmspc {
// AnalysisFunctions::AnalysisFunctions() : TObject() {}
// AnalysisFunctions::~AnalysisFunctions() {}
Double_t AnalysisFunctions::Pol1(double * x, double * par)
{
  return par[0] + x[0] * par[1];
}
TF1 * AnalysisFunctions::Pol1(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, Pol1, xmin, xmax, 2);
  return f;
}
Double_t AnalysisFunctions::Pol2(double * x, double * par)
{
  return par[0] + x[0] * par[1] + x[0] * x[0] * par[2];
}
TF1 * AnalysisFunctions::Pol2(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, Pol2, xmin, xmax, 3);
  return f;
}
Double_t AnalysisFunctions::BreitWigner(double * x, double * par)
{
  return par[0] * TMath::BreitWigner(x[0], par[1], par[2]);
}
TF1 * AnalysisFunctions::BreitWigner(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, BreitWigner, xmin, xmax, 3);
  return f;
}
Double_t AnalysisFunctions::Voigt(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]);
}
TF1 * AnalysisFunctions::Voigt(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, Voigt, xmin, xmax, 4);
  return f;
}

Double_t AnalysisFunctions::VoigtPol1(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]) + Pol1(x, &par[4]);
}
TF1 * AnalysisFunctions::VoigtPol1(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, VoigtPol1, xmin, xmax, 6);
  return f;
}
Double_t AnalysisFunctions::VoigtPol2(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]) + Pol2(x, &par[4]);
}
TF1 * AnalysisFunctions::VoigtPol2(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, VoigtPol2, xmin, xmax, 7);

  return f;
}
Double_t AnalysisFunctions::GausPol1(double * x, double * par)
{
  return par[0] * TMath::Gaus(x[0], par[1], par[2]) + Pol1(x, &par[3]);
}
TF1 * AnalysisFunctions::GausPol1(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, GausPol1, xmin, xmax, 4);
  return f;
}

Double_t AnalysisFunctions::GausPol2(double * x, double * par)
{
  return par[0] * TMath::Gaus(x[0], par[1], par[2]) + Pol2(x, &par[3]);
}
TF1 * AnalysisFunctions::GausPol2(const char * name, double xmin, double xmax)
{
  TF1 * f = new TF1(name, GausPol2, xmin, xmax, 5);
  return f;
}

} // namespace Ndmspc
