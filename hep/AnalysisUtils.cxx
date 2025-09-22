#include <TFitResult.h>
#include <TList.h>
#include <NLogger.h>
#include <cmath>
#include "TMath.h"
#include "AnalysisFunctions.h"
#include "AnalysisUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::AnalysisUtils);
/// \endcond

namespace Ndmspc {

bool AnalysisUtils::ExtractSignal(TH1 * sigBg, TH1 * bg, TF1 * fitFunc, json & cfg, TList * output, TH1 * results)
{
  if (!sigBg) {
    NLogger::Error("Cannot extract signal: sigBg histograms is null");
    return false;
  }

  sigBg->SetName("sigBg");
  bg->SetName("bg");

  TH1 * hPeak = (TH1 *)sigBg->Clone("hPeak");
  if (!hPeak) {
    NLogger::Error("Cannot clone histogram 'sigBg' !!!");
    return false;
  }

  // TODO: Handle correctly skipped output list
  // if (sigBg->Integral() < 100000) {
  //   NLogger::Error("Histogram 'sigBg' has no entries !!!");
  //   delete hPeak;
  //   output->Clear("C");
  //   return false;
  // }

  TH1 * hBgNorm = nullptr;
  if (bg) {
    // Norming the background histogram
    hBgNorm = (TH1 *)bg->Clone("hBgNorm");
    if (!hBgNorm) {
      NLogger::Error("Cannot clone histogram 'bg' !!!");
      return false;
    }

    Int_t binNormMin = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["min"].get<int>());
    Int_t binNormMax = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["max"].get<int>());

    if (binNormMin < 1 || binNormMax > hBgNorm->GetNbinsX()) {
      NLogger::Error("Normalization range is out of bounds: %d - %d", binNormMin, binNormMax);
      return false;
    }

    Double_t scaleFactor = hPeak->Integral(binNormMin, binNormMax) / hBgNorm->Integral(binNormMin, binNormMax);
    // NLogger::Info("Scaling background by factor %.3f", scaleFactor);
    // check id scaleFactor is NaN or Inf
    if (std::isnan(scaleFactor) || std::isinf(scaleFactor)) {
      NLogger::Error("Scale factor is NaN or Inf !!! Skipping normalization ...");
      scaleFactor = 1.0;
    }
    else {
      scaleFactor *= 0.99;
      hBgNorm->Scale(scaleFactor);
    }
  }
  hPeak->Add(hBgNorm, -1);

  double minContent = hPeak->GetMinimum();
  if (minContent < 0) {
    Double_t offset = -minContent + 0.1; // Shift by 0.1 to ensure all bins are non-negative
    NLogger::Trace("Shifting histogram 'hPeak' by %.3f to make all bins non-negative", offset);
    for (int bin = 1; bin <= hPeak->GetNbinsX(); ++bin) {
      Double_t content      = hPeak->GetBinContent(bin);
      Double_t contentSigBg = sigBg->GetBinContent(bin);
      if (TMath::Abs(contentSigBg) < 1e-5) continue;
      // NLogger::Debug("Bin %d: content = %.3f", bin, content + offset);
      hPeak->SetBinContent(bin, content + offset);
    }
  }

  int fitGoodness = -1;
  if (hPeak->Integral() > 0) {

    int nFits     = 100;
    int fitStatus = -1;
    for (int i = 0; i < nFits; ++i) {
      // Ndmspc::NLogger::Info("Fit iteration %d", i);
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
      // NLogger::Info("Fit goodness: %d", isFitGood);
    }
    // if (fitGoodness != 0) {
    if (fitGoodness < 0) {
      // set all parameters to 0
      NLogger::Warning("Fit was not successful, setting all parameters to 0");
      for (int i = 0; i < fitFunc->GetNpar(); ++i) {
        fitFunc->SetParameter(i, 0);
        fitFunc->SetParError(i, 0);
      }
    }

    switch (fitGoodness) {
    case 0: fitFunc->SetLineColor(kGreen); break;
    case 1: fitFunc->SetLineColor(kOrange); break;
    case 2: fitFunc->SetLineColor(kMagenta); break;
    case 3: fitFunc->SetLineColor(kBlue); break;
    case 4: fitFunc->SetLineColor(kCyan); break;
    default: fitFunc->SetLineColor(kGray); break;
    }
    hPeak->GetListOfFunctions()->Add(fitFunc); // Add the function to the histogram's list of functions
  }
  // if (results && isFitGood) {
  if (results) {
    std::vector<std::string> parameters = cfg["parameters"].get<std::vector<std::string>>();
    for (size_t i = 0; i < 4; ++i) {
      // for (size_t i = 0; i < parameters.size(); ++i) {
      results->SetBinContent(i + 1, fitFunc->GetParameter(i));
      results->SetBinError(i + 1, fitFunc->GetParError(i));
    }
    results->SetBinContent(5, fitGoodness + 1);
    results->SetBinError(5, 0.0);
    output->Add(results);
  }

  if (output) {
    output->Add(sigBg);
    if (bg) output->Add(bg);
    if (hBgNorm) output->Add(hBgNorm);
    output->Add(hPeak);
  }
  else {
    delete hPeak;
    delete hBgNorm;
  }

  return true;
}
} // namespace Ndmspc
