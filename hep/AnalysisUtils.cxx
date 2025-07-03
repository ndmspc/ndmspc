#include "AnalysisUtils.h"
#include <TList.h>
#include <NLogger.h>

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
    scaleFactor *= 0.99;
    hBgNorm->Scale(scaleFactor);
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

  int nFits = 10;
  for (int i = 0; i < nFits; ++i) {
    // Ndmspc::NLogger::Info("Fit iteration %d", i);
    hPeak->Fit(fitFunc, "RQN0"); // "S" for fit statistics, "Q" for quiet
  }
  hPeak->GetListOfFunctions()->Add(fitFunc); // Add the function to the histogram's list of functions

  if (results) {
    std::vector<std::string> parameters = cfg["parameters"].get<std::vector<std::string>>();
    for (size_t i = 0; i < parameters.size(); ++i) {
      results->SetBinContent(i + 1, fitFunc->GetParameter(i));
      results->SetBinError(i + 1, fitFunc->GetParError(i));
    }
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
