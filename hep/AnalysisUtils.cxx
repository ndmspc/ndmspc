#include <TFitResult.h>
#include <TList.h>
#include <cmath>
#include <TGraph.h>
#include <TCanvas.h>
#include <TMath.h>
#include <TH1.h>

#include <RooRealVar.h>
#include <RooDataHist.h>
#include <RooVoigtian.h>
#include <RooPolynomial.h>
#include <RooFitResult.h>
#include <RooPlot.h>
#include <RooAddPdf.h>
#include <RooHistPdf.h>
#include <RooChebychev.h>

#include "NUtils.h"

#include "AnalysisFunctions.h"
#include "AnalysisUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::AnalysisUtils);
/// \endcond

namespace Ndmspc {

static std::mutex gRooFitMutex;

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
  int minEntries = cfg.contains("minEntries") ? cfg["minEntries"].get<int>() : 100;
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

    Int_t binNormMin = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["min"].get<double>());
    Int_t binNormMax = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["max"].get<double>());

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

  // double minContent = hPeak->GetMinimum();
  // if (minContent < 0) {
  //   Double_t offset = -minContent + 0.1; // Shift by 0.1 to ensure all bins are non-negative
  //   NLogTrace("Shifting histogram 'hPeak' by %.3f to make all bins non-negative", offset);
  //   for (int bin = 1; bin <= hPeak->GetNbinsX(); ++bin) {
  //     Double_t content      = hPeak->GetBinContent(bin);
  //     Double_t contentSigBg = sigBg->GetBinContent(bin);
  //     if (TMath::Abs(contentSigBg) < 1e-5) continue;
  //     // NLogDebug("Bin %d: content = %.3f", bin, content + offset);
  //     hPeak->SetBinContent(bin, content + offset);
  //   }
  // }
  //
  if (hPeak->Integral() > 0 && accepted) {

    int nFits     = 10;
    int fitStatus = -1;
    for (int i = 0; i < nFits; ++i) {
      // NLogInfo("Fit iteration %d", i);
      fitStatus = hPeak->Fit(fitFunc, "RQN0"); // "Q" for quiet
      // check if fit was successful
      // if (fitFunc->GetNDF() > 0 && fitFunc->GetProb() > 0.001) {
      //   break; // Exit loop if fit is successful
      // }
    }

    if (fitStatus > 0) {
      TFitResultPtr fitResults = hPeak->Fit(fitFunc, "QRNS");
      // TFitResultPtr fitResults = hPeak->Fit(fitFunc, "QRNMS");
      // check if fit was successful
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
      // accepted    = false;
    }

    // NLogInfo("Fit goodness: %d", fitGoodness);
    fitFunc->SetLineColor(fitGoodness + 1);
    hPeak->GetListOfFunctions()->Add(fitFunc); // Add the function to the histogram's list of functions
  }
  else {
    accepted = false;
  }

  if (results && accepted && fitGoodness >= 0) {
    std::vector<std::string> parameters = cfg["parameters"].get<std::vector<std::string>>();
    // for (size_t i = 0; i < 4; ++i) {
    for (size_t i = 0; i < parameters.size(); ++i) {
      results->SetBinContent(i + 1, fitFunc->GetParameter(i));
      // results->SetBinError(i + 1, 0.0);
      // NLogInfo("Parameter %s: %.5f ± %.5f", parameters[i].c_str(), fitFunc->GetParameter(i),
      // fitFunc->GetParError(i));
      results->SetBinError(i + 1, fitFunc->GetParError(i));

      // if (TMath::IsNaN(fitFunc->GetParError(i)) || TMath::IsNaN(fitFunc->GetParameter(i))) {
      //   NLogWarning("Parameter %s has NaN or Inf error, setting error to 0", parameters[i].c_str());
      //   results->SetBinError(i + 1, 0.0);
      //   accepted = false;
      // }
      // if (fitFunc->GetParError(i) > fitFunc->GetParameter(i) / 1000.) {
      //   accepted = false;
      // }
    }
    // results->SetBinContent(5, fitGoodness + 1);
    // results->SetBinError(5, 0.0);
  }

  if (output) {
    if (sigBg) output->Add(sigBg);
    if (bg) output->Add(bg);
    if (hBgNorm) output->Add(hBgNorm);
    if (hPeak) output->Add(hPeak);
    // if (results) output->Add(results);
  }

  if (!accepted) {
    NLogWarning("Signal extraction was not fully successful");
    ResetHistograms(output);
  }

  return accepted;
}

bool AnalysisUtils::ExtractSignalRooFit(TH1 * sigBg, TH1 * bg, json & cfg, TList * output, TH1 * results)
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
  int minEntries = cfg.contains("minEntries") ? cfg["minEntries"].get<int>() : 100;
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
  Double_t scaleFactor = 1.0;
  if (bg) {
    bg->SetName("bg");
    //   // Norming the background histogram
    hBgNorm = (TH1 *)bg->Clone("hBgNorm");
    if (!hBgNorm) {
      NLogError("Cannot clone histogram 'bg' !!!");
      // return false;
      accepted = false;
    }

    Int_t binNormMin = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["min"].get<double>());
    Int_t binNormMax = hBgNorm->GetXaxis()->FindBin(cfg["norm"]["max"].get<double>());

    if (binNormMin < 1 || binNormMax > hBgNorm->GetNbinsX()) {
      NLogError("Normalization range is out of bounds: %d - %d", binNormMin, binNormMax);
      // return false;
      accepted = false;
    }

    scaleFactor = hPeak->Integral(binNormMin, binNormMax) / hBgNorm->Integral(binNormMin, binNormMax);
    // NLogInfo("Scaling background by factor %.3f", scaleFactor);
    // check id scaleFactor is NaN or Inf
    if (std::isnan(scaleFactor) || std::isinf(scaleFactor)) {
      NLogError("Scale factor is NaN or Inf !!! Skipping normalization ...");
      scaleFactor = 1.0;
      accepted    = false;
    }
    else {
      // scaleFactor *= 0.99;
      // hBgNorm->Scale(scaleFactor);
      // std::cout << "BG1 scale factor: " << scaleFactor << std::endl;
    }
  }

  TString fitType = cfg["fitType"].get<std::string>().c_str();
  fitType.ToLower();
  if (!fitType.CompareTo("rootfit")) {
    NLogInfo("Performing RooFit-based signal extraction");

    // hPeak->Add(hBgNorm, -1);

    // std::cout << "peak  integral: " << hPeak->Integral() << std::endl;
    // std::cout << "peak  mean    : " << hPeak->GetMean() << std::endl;

    Double_t min = hPeak->GetXaxis()->GetXmin();
    Double_t max = hPeak->GetXaxis()->GetXmax();

    // ROOT::EnableThreadSafety();
    RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);
    RooMsgService::instance().setSilentMode(kTRUE);

    // min = 0.99;
    // max = 1.05;

    // // 1. Create all RooFit objects locally on the stack within this thread.
    // // This ensures no other thread touches these specific memory addresses.
    // RooRealVar mass("mass", "MeV/c^2", min, max);

    // RooDataHist dataHist("dataHist", "Data", RooArgSet(mass), hPeak);
    // RooDataHist mixedHist("mixedHist", "Mixed", RooArgSet(mass), hBgNorm);
    // RooHistPdf  bkgPdf("bkgPdf", "Bkg PDF", RooArgSet(mass), mixedHist);

    // RooRealVar  mean("mean", "mean", 1.019, 1.000, 1.050);
    // RooRealVar  sigma("sigma", "sigma", 0.01, 0.005, 0.05);
    // RooVoigtian sigPdf("sigPdf", "Sig PDF", mass, mean, 0.005, sigma);

    // RooRealVar nSig("nSig", "nSig", 100, 0, 100000);
    // RooRealVar nBkg("nBkg", "nBkg", 1000, 0, 1000000);
    // RooAddPdf  model("model", "Model", RooArgList(sigPdf, bkgPdf), RooArgList(nSig, nBkg));

    // // 2. Perform the fit.
    // // IMPORTANT: Set RooFit::PrintLevel(-1) to suppress stdout
    // // so threads don't spam the terminal and cause garbled text output.
    // model.fitTo(dataHist, RooFit::Extended(kTRUE), RooFit::PrintLevel(-1));

    // // 3. Extract your yield safely (e.g., store it in a thread-safe container)
    // // You could return this to a concurrent-safe list or save to a file
    // printf("Thread finished. Signal Yield: %f +/- %f\n", nSig.getVal(), nSig.getError());

    Double_t minFit = min;
    Double_t maxFit = max;
    // // minFit          = fitMin;
    // // maxFit          = fitMax;
    minFit = 0.997;
    // maxFit = 1.050;
    maxFit = 1.060;

    // minFit = 1.000;
    // maxFit = 1.040;

    // std::cout << "hSigBg integral (fit range): "
    //           << hPeak->Integral(hPeak->GetXaxis()->FindBin(minFit), hPeak->GetXaxis()->FindBin(maxFit)) << std::endl;
    // std::cout << "hBgNorm integral (fit range): "
    //           << hBgNorm->Integral(hBgNorm->GetXaxis()->FindBin(minFit), hBgNorm->GetXaxis()->FindBin(maxFit))
    //           << std::endl;
    // std::cout << "BG scale factor: " << scaleFactor << std::endl;

    // TH1 * hBgScaled = (TH1 *)hBgNorm->Clone("hBgScaled");
    hBgNorm->Scale(scaleFactor);

    // RooRealVar x("x", "x", minFit, maxFit);
    RooRealVar x("x", "x", min, max);
    // // Define a named sub-range for fitting

    x.setRange("fullRange", min, max);
    x.setRange("fitRange", minFit, maxFit);

    // RooRealVar x("x", "x", minFit, maxFit);
    // RooDataHist data("data", "dataset with x", x, (TH1 *)hPeak->Clone());
    RooDataHist data("data", "dataset with x", x, hPeak);

    // std::cout << "hBgNorm integral AFTER scale, fit range: "
    //           << hBgNorm->Integral(hBgNorm->GetXaxis()->FindBin(minFit), hBgNorm->GetXaxis()->FindBin(maxFit))
    //           << std::endl;
    // std::cout << "hPeak   integral, fit range: "
    //           << hPeak->Integral(hPeak->GetXaxis()->FindBin(minFit), hPeak->GetXaxis()->FindBin(maxFit)) << std::endl;

    // Use hBgNorm directly as background shape PDF
    RooDataHist bgHist("bgHist", "background histogram", x, hBgNorm);
    RooHistPdf  bgPdf("bgPdf", "background pdf", RooArgSet(x), bgHist, 2);

    // //	if (kVoightPol1)
    RooRealVar meanV("mean", "mean Voigtian", 1.019, 1.010, 1.030);
    RooRealVar widthV("width", "width Voigtian", 0.0045, 0.001, 0.010);
    // RooRealVar  sigmaV("sigma", "sigma Voigtian", 0.001, 0.00001, 0.010);
    RooRealVar sigmaV("sigma", "sigma Voigtian", 0.001);
    sigmaV.setConstant(kTRUE);
    RooVoigtian sig("voigtian", "Voigtian", x, meanV, widthV, sigmaV);

    // RooRealVar c0("c0", "coefficient #0", 1.0, -10., 10.);
    // RooRealVar c1("c1", "coefficient #1", 0.1, -10., 10.);
    // RooRealVar c2("c2", "coefficient #2", -0.1, -10., 10.);
    // RooPolynomial bgPdf("bgPdf", "background p.d.f.", x, RooArgList(c0, c1));
    // RooPolynomial bgPdf("bgPdf", "background p.d.f.", x, RooArgList(c0, c1, c2));
    // //	RooChebychev bgPdf("bgPdf","background p.d.f.",x,RooArgList(c0,c1)) ;
    // RooChebychev bgPdf("bgPdf", "background p.d.f.", x, RooArgList(c0, c1, c2));

    // RooRealVar fsig("yield", "signal fraction", 0.5, 0., 1.);
    // RooAddPdf  model("model", "model", RooArgList(sig, bkg), fsig);

    // // --- Construct signal+background PDF ---
    // RooRealVar nsig("yield", "#signal events", 200, 0., 1e8);
    // RooRealVar nbkg("nbkg", "#background events", 800, 0., 1e8);
    // RooAddPdf  model("model", "v+p", RooArgList(sig, bgPdf), RooArgList(nsig, nbkg));

    // Initial yields based on actual data in fit range
    Double_t totalEvents = hPeak->Integral(hPeak->GetXaxis()->FindBin(minFit), hPeak->GetXaxis()->FindBin(maxFit));
    Double_t bgEvents = hBgNorm->Integral(hBgNorm->GetXaxis()->FindBin(minFit), hBgNorm->GetXaxis()->FindBin(maxFit));
    // hPeak->Add(hBgNorm, -1);
    // RooRealVar nsig("yield", "#signal events", totalEvents * 0.5, 0., totalEvents * 2);
    // RooRealVar nbkg("nbkg", "#background events", totalEvents * 0.5, 0., totalEvents * 2);
    // RooAddPdf  model("model", "v+p", RooArgList(sig, bkg), RooArgList(nsig, nbkg));

    RooRealVar nsig("yield", "#signal events", totalEvents - bgEvents, 0., totalEvents * 2);
    RooRealVar nbkg("nbkg", "#background events", bgEvents, 0., totalEvents * 2);
    RooAddPdf  model("model", "sig+bg", RooArgList(sig, bgPdf), RooArgList(nsig, nbkg));

    // std::cout << "peak integral in fit range: "
    //           << hPeak->Integral(hPeak->GetXaxis()->FindBin(minFit), hPeak->GetXaxis()->FindBin(maxFit)) << std::endl;
    // std::cout << "=======================" << std::endl;

    RooFitResult * fitResult = 0;

    {
      // std::lock_guard<std::mutex> lock(gRooFitMutex);
      // // fitResult = model.chi2FitTo(data, RooFit::Save(), RooFit::SumW2Error(kTRUE), RooFit::Range(minFit, maxFit),
      // // RooFit::PrintLevel(-1)); fitResult = model.fitTo(data, RooFit::Save(),RooFit::SumW2Error(kTRUE),
      // // RooFit::Range(minFit,maxFit),RooFit::PrintLevel(-1));
      fitResult = model.fitTo(data, RooFit::Range("fitRange"), RooFit::PrintLevel(-1), RooFit::Save(),
                              RooFit::Parallelize(0), RooFit::SumW2Error(kTRUE), RooFit::Extended(kTRUE));
    }
    if (!fitResult) {
      accepted = false;
      NLogError("RooFit failed to return a fit result object !!!");
    }
    else {
      // static std::mutex           fitMutex;
      // std::lock_guard<std::mutex> lock(fitMutex);
      NLogInfo("RooFit fit status: %d, cov. matrix status: %d", fitResult->status(), fitResult->covQual());
      // fitResult->Print();
    }

    std::vector<std::string> parameters = cfg["parameters"].get<std::vector<std::string>>();
    const RooArgList &       params     = fitResult->floatParsFinal();
    // for (size_t i = 0; i < 4; ++i) {
    for (size_t i = 0; i < parameters.size(); ++i) {
      RooRealVar * p = (RooRealVar *)params.find(parameters[i].c_str());
      if (p) {
        results->SetBinContent(i + 1, p->getVal());
        results->SetBinError(i + 1, p->getError());
      }
    }

    // RooPlot * frame = x.frame(RooFit::Title("RooFit Signal Extraction"));
    RooPlot * frame = x.frame(RooFit::Range("fullRange"));
    frame->SetDirectory(0); // Detach frame from any file to avoid ownership issues
    data.plotOn(frame);
    model.plotOn(frame, RooFit::LineColor(kRed), RooFit::NormRange("fitRange"));
    model.plotOn(frame, RooFit::Components("bgPdf"), RooFit::LineStyle(kDashed), RooFit::LineColor(kBlue),
                 RooFit::NormRange("fitRange"));
    model.plotOn(frame, RooFit::Components("voigtian"), RooFit::LineStyle(kDotted), RooFit::LineColor(kGreen + 2),
                 RooFit::NormRange("fitRange"));

    // TCanvas * c = new TCanvas("c", "c", 800, 600);
    TCanvas * c = Ndmspc::NUtils::CreateCanvas("cPeak", "cPeak");
    frame->Draw();
    output->Add(c);

    // output-> Add(frame);
  }
  else if (!fitType.CompareTo("std")) {
    NLogInfo("Performing custom TF1-based signal extraction");

    hPeak->Add(hBgNorm, -1);
    if (hPeak->Integral() > 0 && accepted) {
      TF1 * fitFunc = Ndmspc::AnalysisFunctions::VoigtPol2("fVoigtPol2", 0.998, 1.042);
      fitFunc->SetParameters(0.0, 1.019461, 0.00426, 0.0008, 0.0, 0.0, 0.0);

      int nFits     = 10;
      int fitStatus = -1;
      for (int i = 0; i < nFits; ++i) {
        // NLogInfo("Fit iteration %d", i);
        fitStatus = hPeak->Fit(fitFunc, "RQN0"); // "Q" for quiet
        // check if fit was successful
        // if (fitFunc->GetNDF() > 0 && fitFunc->GetProb() > 0.001) {
        //   break; // Exit loop if fit is successful
        // }
      }

      if (fitStatus > 0) {
        TFitResultPtr fitResults = hPeak->Fit(fitFunc, "QRNS");
        // TFitResultPtr fitResults = hPeak->Fit(fitFunc, "QRNMS");
        // check if fit was successful
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
        // accepted    = false;
      }

      // NLogInfo("Fit goodness: %d", fitGoodness);
      fitFunc->SetLineColor(fitGoodness + 1);
      hPeak->GetListOfFunctions()->Add(fitFunc); // Add the function to the histogram's list of functions
    }
    else {
      accepted = false;
    }
  }
  else {

    NLogError("Unknown fit type '%s' specified in configuration", fitType.Data());
    return false;
  }

  if (output) {
    if (sigBg) output->Add(sigBg);
    if (bg) output->Add(bg);
    if (hBgNorm) output->Add(hBgNorm);
    if (hPeak) output->Add(hPeak);
    // if (results) output->Add(results);
  }

  //

  // NLogWarning("RooFit-based signal extraction is not implemented yet !!!");
  return true;
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
