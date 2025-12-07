#include "AnalysisFunctions.h"
#include <TMath.h>
#include <TF1.h>
#include <TFitResult.h>
#include "NLogger.h"

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

int AnalysisFunctions::IsFitGood(TF1 * func, TFitResultPtr fitResult, double chi2nMin, double chi2nMax, double probMin,
                                 double corrMax)
{
  if (fitResult.Get() == nullptr) {
    NLogWarning("Fit result is null");
    return false;
  }
  int status = fitResult->Status();

  // 1. Chi-squared (Chi^2) and Number of Degrees of Freedom (NDF)
  double chi2 = fitResult->Chi2();
  int    ndf  = fitResult->Ndf();
  double prob = fitResult->Prob();

  if (status < 0) {
    NLogWarning("Fit did not converge properly, status = %d", status);
    return status;
  }

  if (ndf <= 0) {
    NLogWarning("Fit has non-positive degrees of freedom, ndf = %d", ndf);
    return 4;
  }
  // 2. Reduced Chi-squared (Chi^2 / NDF)
  // A value close to 1 generally indicates a good fit.
  // Values significantly larger than 1 suggest the model does not describe
  // the data well, or errors are underestimated.
  // Values significantly smaller than 1 might indicate overestimated errors
  // or too many free parameters (overfitting).
  double chi2n = chi2 / ndf;
  if (chi2n < chi2nMin || chi2n > chi2nMax) {
    NLogWarning("Fit has poor chi2/ndf = %E (min: %.3f, max: %.3f)", chi2n, chi2nMin, chi2nMax);
    return 3;
  }
  // 3. p-value
  // The probability of observing data at least as extreme as that observed,
  // assuming the null hypothesis (the model describes the data) is true.
  // A low p-value (e.g., < 0.05) suggests the model is a poor description.
  // ROOT's TFitResult gives a p-value.
  if (prob < probMin) {
    NLogWarning("Fit has low probability = %E (min: %.4f)", prob, probMin);
    return 2;
  }
  // 5. Correlation Matrix
  // High correlations between parameters (close to +1 or -1) can indicate
  // that the parameters are not well-determined independently. This can
  // lead to large uncertainties and make the fit unstable.

  for (int i = 0; i < func->GetNpar(); ++i) {
    for (int j = i + 1; j < func->GetNpar(); ++j) {
      double correlation = fitResult->Correlation(i, j);
      if (std::abs(correlation) > corrMax) { // Highlight high correlations
        NLogWarning("Fit has high correlation (%.2f) between parameters %s and %s", correlation,
                         func->GetParName(i), func->GetParName(j));
        return 1;
      }
    }
  }

  NLogInfo("Fit is good: chi2/ndf=%.2f, prob=%.4f", chi2n, prob);

  return 0;
}

} // namespace Ndmspc
