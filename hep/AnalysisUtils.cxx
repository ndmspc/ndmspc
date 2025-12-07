#include <TFitResult.h>
#include <TList.h>
#include <NLogger.h>
#include <cmath>
#include <TH1.h>
#include <TGraph.h>
#include <TMath.h>
#include "AnalysisFunctions.h"
#include "AnalysisUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::AnalysisUtils);
/// \endcond

namespace Ndmspc {

bool AnalysisUtils::ExtractSignal(TH1 * sigBg, TH1 * bg, TF1 * fitFunc, json & cfg, TList * output, TH1 * results)
{

  bool accepted = true;

  // Initializing variables
  TH1 * hBgNorm     = nullptr;
  TH1 * hPeak       = nullptr;
  int   fitGoodness = -1;

  if (sigBg) sigBg->SetName("sigBg");
  if (!sigBg) {
    NLogError("Cannot extract signal: sigBg histograms is null");
    // return false;
    accepted = false;
  }

  // TODO: Handle correctly skipped output list
  int minEntries = 10000;
  if (sigBg->Integral() < minEntries) {
    NLogWarning("Histogram 'sigBg' has only %d entries, minimum is %d", (int)sigBg->Integral(), minEntries);
    accepted = false;
  }

  hPeak = (TH1 *)sigBg->Clone("hPeak");
  if (!hPeak) {
    NLogError("Cannot clone histogram 'sigBg' !!!");
    // return false;
    accepted = false;
  }

  if (bg) {
    bg->SetName("bg");
    // Norming the background histogram
    hBgNorm = (TH1 *)bg->Clone("hBgNorm");
    if (!hBgNorm) {
      NLogError("Cannot clone histogram 'bg' !!!");
      // return false;
      accepted = false;
    }

    Int_t binNormMin = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["min"].get<int>());
    Int_t binNormMax = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["max"].get<int>());

    if (binNormMin < 1 || binNormMax > hBgNorm->GetNbinsX()) {
      NLogError("Normalization range is out of bounds: %d - %d", binNormMin, binNormMax);
      // return false;
      accepted = false;
    }

    Double_t scaleFactor = hPeak->Integral(binNormMin, binNormMax) / hBgNorm->Integral(binNormMin, binNormMax);
    // NLogInfo("Scaling background by factor %.3f", scaleFactor);
    // check id scaleFactor is NaN or Inf
    if (std::isnan(scaleFactor) || std::isinf(scaleFactor)) {
      NLogError("Scale factor is NaN or Inf !!! Skipping normalization ...");
      scaleFactor = 1.0;
      accepted    = false;
    }
    else {
      scaleFactor *= 0.99;
      hBgNorm->Scale(scaleFactor);
    }
    hPeak->Add(hBgNorm, -1);
  }

  double minContent = hPeak->GetMinimum();
  if (minContent < 0) {
    Double_t offset = -minContent + 0.1; // Shift by 0.1 to ensure all bins are non-negative
    NLogTrace("Shifting histogram 'hPeak' by %.3f to make all bins non-negative", offset);
    for (int bin = 1; bin <= hPeak->GetNbinsX(); ++bin) {
      Double_t content      = hPeak->GetBinContent(bin);
      Double_t contentSigBg = sigBg->GetBinContent(bin);
      if (TMath::Abs(contentSigBg) < 1e-5) continue;
      // NLogDebug("Bin %d: content = %.3f", bin, content + offset);
      hPeak->SetBinContent(bin, content + offset);
    }
  }
  //
  if (hPeak->Integral() > 0 && accepted) {

    int nFits     = 100;
    int fitStatus = -1;
    for (int i = 0; i < nFits; ++i) {
      // NLogInfo("Fit iteration %d", i);
      fitStatus = hPeak->Fit(fitFunc, "RQN0"); // "S" for fit statistics, "Q" for quiet
      // check if fit was successful
      // if (fitFunc->GetNDF() > 0 && fitFunc->GetProb() > 0.001) {
      //   break; // Exit loop if fit is successful
      // }
    }

    if (fitStatus > 0) {
      TFitResultPtr fitResults = hPeak->Fit(fitFunc, "QRNS");
      // bool          isFitGood  = Ndmspc::AnalysisFunctions::IsFitGood(fitFunc, fitResults, 0.5, 2.0, 0.001, 0.8);
      fitGoodness = Ndmspc::AnalysisFunctions::IsFitGood(fitFunc, fitResults, 0.5, 500.0, 0.00001, 1.0);
      // fitGoodness = 0; // TEMPORARY OVERRIDE
      // NLogInfo("Fit goodness: %d", isFitGood);
    }
    // if (fitGoodness != 0) {
    if (fitGoodness < 0) {
      // set all parameters to 0
      NLogWarning("Fit was not successful, setting all parameters to 0");
      for (int i = 0; i < fitFunc->GetNpar(); ++i) {
        fitFunc->SetParameter(i, 0);
        fitFunc->SetParError(i, 0);
      }
      fitGoodness = -1;
    }

    fitFunc->SetLineColor(fitGoodness + 1);
    hPeak->GetListOfFunctions()->Add(fitFunc); // Add the function to the histogram's list of functions
  }
  else {
    accepted = false;
  }

  if (results && accepted && fitGoodness >= 0) {
    std::vector<std::string> parameters = cfg["parameters"].get<std::vector<std::string>>();
    for (size_t i = 0; i < 4; ++i) {
      // for (size_t i = 0; i < parameters.size(); ++i) {
      results->SetBinContent(i + 1, fitFunc->GetParameter(i));
      results->SetBinError(i + 1, fitFunc->GetParError(i));
    }
    results->SetBinContent(5, fitGoodness + 1);
    results->SetBinError(5, 0.0);
  }

  if (output) {
    if (sigBg) output->Add(sigBg);
    if (bg) output->Add(bg);
    if (hBgNorm) output->Add(hBgNorm);
    if (hPeak) output->Add(hPeak);
    if (results) output->Add(results);
  }

  if (!accepted) {
    NLogWarning("Signal extraction was not fully successful");
    ResetHistograms(output);
  }

  return accepted;
}

void AnalysisUtils::ResetHistograms(TList * list)
{
  ///
  /// Reset histograms in the list
  ///

  if (!list) return;
  TIter next(list);
  TH1 * h = nullptr;
  while ((h = dynamic_cast<TH1 *>(next()))) {
    h->Reset();
  }
}
} // namespace Ndmspc
