#include <TCanvas.h>
#include <TF1.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TROOT.h>
#include <TString.h>
#include <ndmspc/PointRun.h>
#include <ndmspc/Utils.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

TList * _currentFunctions = nullptr;

TList * RsnFunctions(std::string name, Double_t min, Double_t max, bool reuseFunctions = false);
bool    ProcessFit(json & cfg, TH1 * peak, int verbose = 0);
bool    RsnPhiPointMacro(NdmSpc::PointRun * pr)
{

  json                     cfg          = pr->Cfg();
  TList *                  inputList    = pr->GetInputList();
  THnSparse *              resultObject = pr->GetResultObject();
  Int_t *                  point        = pr->GetCurrentPoint();
  std::vector<std::string> pointLabels  = pr->GetCurrentPointLabels();
  json                     pointValue   = pr->GetCurrentPointValue();
  TList *                  outputList   = pr->GetOutputList();

  int projectionId = cfg["user"]["proj"].get<int>();

  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }

  if (inputList == nullptr) return false;

  THnSparse * sSigBg    = (THnSparse *)inputList->FindObject("sigBg");
  THnSparse * sBgLike   = (THnSparse *)inputList->FindObject("bgLike");
  THnSparse * sBgMixing = (THnSparse *)inputList->FindObject("bgMixing");
  THnSparse * sMcTrue   = (THnSparse *)inputList->FindObject("mcTrue");
  THnSparse * sMcGen    = (THnSparse *)inputList->FindObject("mcGen");

  TH1D * hSigBg  = nullptr;
  TH1D * hBg     = nullptr;
  TH1 *  hBgNorm = nullptr;
  TH1 *  hPeak   = nullptr;
  TH1D * hMcTrue = nullptr;
  TH1D * hMcGen  = nullptr;

  if (sSigBg) {
    hSigBg = sSigBg->Projection(projectionId, "O");
    // Skip bin (do not save any output)
    if (hSigBg->GetEntries() < cfg["user"]["minEntries"].get<int>()) {
      /*skipCurrentBin = true;*/
      pr->SetSkipCurrentBin(true);
      return false;
    }
  }
  else {
    Printf("Error: Object 'sigBg' was not found !!!");
    /*processExit = true;*/
    pr->SetProcessExit(true);
    return false;
  }

  if (sBgLike && !pointValue["bg"].get<std::string>().compare("like")) {
    hBg = sBgLike->Projection(projectionId, "O");
  }
  else if (sBgMixing && !pointValue["bg"].get<std::string>().compare("mixing")) {
    hBg = sBgMixing->Projection(projectionId, "O");
  }
  else {
    if (verbose >= 1)
      Printf("Skipping because of '%s' bg was not found ...", pointValue["bg"].get<std::string>().c_str());
    return false;
  }

  TH1 * hBgLikeNorm   = nullptr;
  TH1 * hPeakBgLike   = nullptr;
  TH1 * hBgMixingNorm = nullptr;
  TH1 * hPeakBgMixing = nullptr;
  TH1 * hPeakNoBg     = nullptr;

  if (sMcTrue) hMcTrue = sMcTrue->Projection(projectionId, "O");
  if (sMcGen) hMcGen = sMcGen->Projection(projectionId, "O");

  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();

  if (hSigBg) {
    hSigBg->SetNameTitle("hSigBg", TString::Format("SigBg : %s", titlePostfix.Data()).Data());
    outputList->Add(hSigBg);
  }
  if (hBg) {
    hBg->SetNameTitle("hBg", TString::Format("Bg : %s", titlePostfix.Data()).Data());
    outputList->Add(hBg);
  }
  if (hMcTrue) {
    hMcTrue->SetNameTitle("hMcTrue", TString::Format("McTrue : %s", titlePostfix.Data()).Data());
    outputList->Add(hMcTrue);
  }
  if (hMcGen) {
    hMcGen->SetNameTitle("hMcGen", TString::Format("McGen : %s", titlePostfix.Data()).Data());
    outputList->Add(hMcGen);
  }

  // int start = pointLabels.size() - pointValue.size() - 1;
  // Printf("Current : bg=%d fitFun=%d fitRange=%d norm=%d", point[start + 1], point[start + 2], point[3],
  //        point[start + 4]);
  // Printf("Current : bg=%s fitFun=%s fitRange=%s norm=%s", pointLabels[start + 1].c_str(),
  //        pointLabels[start + 2].c_str(), pointLabels[start + 3].c_str(), pointLabels[start + 4].c_str());

  // Printf("Current: pointValue %s", pointValue.dump().c_str());

  // Setting norm range
  Int_t binNormMin = hSigBg->GetXaxis()->FindBin(pointValue["norm"]["range"][0].get<double>());
  Int_t binNormMax = hSigBg->GetXaxis()->FindBin(pointValue["norm"]["range"][1].get<double>());

  Double_t norm;
  bool     ok;
  // Handle LikeSign Bg
  hBgNorm = (TH1 *)hBg->Clone();
  if (hBgNorm) {
    hBgNorm->SetNameTitle("hBgNorm", TString::Format("BgNorm : %s", titlePostfix.Data()).Data());
    outputList->Add(hBgNorm);
  }
  hPeak = (TH1 *)hSigBg->Clone();
  if (hPeak) {
    hPeak->SetNameTitle("hPeak", TString::Format("Peak : %s", titlePostfix.Data()).Data());
    outputList->Add(hPeak);
  }

  norm = hPeak->Integral(binNormMin, binNormMax) / hBgNorm->Integral(binNormMin, binNormMax);
  hBgNorm->Scale(norm); // scaling the bgProjNorm histogram
  hPeak->Add(hBgNorm, -1);

  // Setting fitting range
  Double_t fitMin = pointValue["fitRange"]["range"][0].get<double>();
  Double_t fitMax = pointValue["fitRange"]["range"][1].get<double>();

  // Printf("%p %f %f", (void *)peak, fitMin, fitMax);

  Int_t    bin_min = hPeak->GetXaxis()->FindBin(fitMin);
  Int_t    bin_max = hPeak->GetXaxis()->FindBin(fitMax);
  Double_t eps     = 1e-5; // maybe put to cfg

  // Some initial values for fitting
  Double_t       p0p       = cfg["user"]["fit"]["default"][0].get<double>();
  const Double_t phi_mass  = cfg["user"]["fit"]["default"][1].get<double>();
  const Double_t phi_width = cfg["user"]["fit"]["default"][2].get<double>();
  const Double_t phi_sigma = cfg["user"]["fit"]["default"][3].get<double>();

  TList * funs = RsnFunctions(pointValue["fitFun"].get<std::string>(), fitMin, fitMax);
  if (funs == nullptr) {
    Printf("Error: Functions were not found for '%s' !!! Exiting ...", pointValue["fitFun"].get<std::string>().c_str());
    return false;
  }
  TF1 * sigBgFnc = (TF1 *)funs->At(0);
  TF1 * bgFnc    = (TF1 *)funs->At(1);

  // p0p = hPeak->Integral(hPeak->FindBin(fitMin), hPeak->FindBin(fitMax)) * hPeak->GetBinWidth(peak->FindBin(fitMin));

  // Sets init parameters for fitting
  sigBgFnc->SetParameters(p0p, phi_mass, phi_width, phi_sigma, 0.0, 0.0, 0.0, 0.0);
  // Fixing parameter for resolution
  sigBgFnc->FixParameter(3, phi_sigma);

  // Doing fit
  Int_t nFit = 10;
  Int_t fitStatus;
  for (Int_t iFit = 0; iFit < nFit; iFit++) {
    fitStatus = hPeak->Fit(sigBgFnc, "QN MFC", "", fitMin, fitMax);
    // Printf("Error: Fit : %d ", fitStatus);
    if (fitStatus < 0) {
      Printf("Error: Fit is not successfull !!!");
      return false;
    }
  }

  TFitResultPtr fFitResult = hPeak->Fit(sigBgFnc, "QN MF S", "", fitMin, fitMax);
  fitStatus                = fFitResult;
  // Printf("Error: Fit : %d ", fitStatus);
  if (fitStatus < 0) {
    Printf("Error: Final fit is not successfull !!!");
    return false;
  }
  Double_t par[7];
  sigBgFnc->GetParameters(par);
  const Double_t * parErr = sigBgFnc->GetParErrors();
  TFitResult *     ftr    = (TFitResult *)fFitResult.Get()->Clone();
  // Double_t errFnc = sigBgFnc->IntegralError(
  // fitMin, fitMax, fFitResult->GetParams(),
  // fFitResult->GetCovarianceMatrix().GetMatrixArray(), 1e-5);
  Double_t integralFnc = sigBgFnc->Integral(fitMin, fitMax, eps);
  Double_t errFnc =
      sigBgFnc->IntegralError(fitMin, fitMax, ftr->GetParams(), ftr->GetCovarianceMatrix().GetMatrixArray());

  if (verbose >= 1) Printf("Function error %f", errFnc);

  // Getting pol2 part from fit and set it to bgFnc for drawing and then
  // subtracting
  bgFnc->SetParameters(&par[4]);

  if (verbose >= 1) ftr->Print("V");

  hPeak->GetListOfFunctions()->Add(sigBgFnc);
  hPeak->GetListOfFunctions()->Add(bgFnc);

  Double_t integral;
  Double_t bg;
  Double_t err;
  Double_t histWidth = hPeak->GetXaxis()->GetBinWidth(1);
  Long64_t frbin;

  integral = hPeak->IntegralAndError(bin_min, bin_max, err);
  // Printf("Integral = %f +/- %f", integral, err);
  if (verbose >= 1) Printf("histWidth = %f", histWidth);

  Double_t epsIntegral = 1e-5;
  // Double_t epsIntegralError = -1;
  // epsIntegralError          = 1e-1;

  bg = bgFnc->Integral(fitMin, fitMax, epsIntegral);
  // Printf("Background = %f", bg);

  bg /= histWidth;
  if (verbose >= 1) Printf("Background/width = %f", bg);
  integral -= bg;
  if (verbose >= 1) Printf("signal = %f", integral);

  // To do : Calculate errFnc error corectly, when dividing by histWidth
  if (verbose >= 1) Printf("integral fnc = %f+/- %f", integralFnc, errFnc);
  integralFnc /= histWidth;
  errFnc /= histWidth;

  integralFnc -= bg;
  if (verbose >= 1) Printf("signal fnc = %f", integralFnc);

  TH1D *   hTrue = nullptr;
  TH1D *   hGen  = nullptr;
  TH1 *    pEff  = nullptr;
  Double_t genIntegral, genErr;
  Double_t trueIntegral, trueErr;

  if (hMcTrue && hMcGen) {
    if (verbose >= 1) Printf("Processing MC histograms ...");

    hTrue = new TH1D("hTrue", "True #varphi", 1, 0., 1.);
    hGen  = new TH1D("hGen", "Gen #varphi", 1, 0., 1.);

    trueIntegral = hMcTrue->IntegralAndError(0, -1, trueErr);
    genIntegral  = hMcGen->IntegralAndError(0, -1, genErr);

    hTrue->SetBinContent(1, trueIntegral);
    hTrue->SetBinError(1, trueErr);
    hGen->SetBinContent(1, genIntegral);
    hGen->SetBinError(1, genErr);

    pEff = (TH1 *)hTrue->Clone();
    pEff->Reset();
    pEff->Divide(hTrue, hGen, 1, 1, "b");

    Printf("true=%.3f gen=%.3f eff=%.3f", hTrue->GetBinContent(1), hGen->GetBinContent(1), pEff->GetBinContent(1));
  }

  // ["RawBC", "RawFnc", "Mass", "Width", "Sigma" , "Chi2", "Probability", "True", "Gen", "Eff"]
  // Int_t nCuts=   pointLabels.size()

  if (resultObject) {
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "RawBC", point, integral, err, false, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "RawFnc", point, integralFnc, errFnc, false, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "RawBCNorm", point, integral, err, true, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "RawFncNorm", point, integralFnc, errFnc, true, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Mass", point, sigBgFnc->GetParameter(1),
                                       sigBgFnc->GetParError(1), false, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Width", point, sigBgFnc->GetParameter(2),
                                       sigBgFnc->GetParError(2), false, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Sigma", point, sigBgFnc->GetParameter(3),
                                       sigBgFnc->GetParError(3), false, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Chi2", point, sigBgFnc->GetChisquare(), 0.0, false, true);
    NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Probability", point, sigBgFnc->GetProb(), 0.0, false, true,
                                       10);
    if (hTrue)
      NdmSpc::Utils::SetResultValueError(cfg, resultObject, "True", point, hTrue->GetBinContent(1),
                                         hTrue->GetBinError(1), true, true);
    if (hGen)
      NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Gen", point, hGen->GetBinContent(1), hGen->GetBinError(1),
                                         true, true);
    if (pEff)
      NdmSpc::Utils::SetResultValueError(cfg, resultObject, "Eff", point, pEff->GetBinContent(1), pEff->GetBinError(1),
                                         false, true);
  }
  else {
    Printf("Error: 'resultObject' was not found !!!");
  }

  // if (!gROOT->IsBatch() && !cfg["ndmspc"]["process"]["type"].get<std::string>().compare("single")) {
  //   // h->DrawCopy();
  //   if (hBgLike) {
  //     TCanvas * cBgLike = new TCanvas("cBgLike", "cBgLike");
  //     cBgLike->Divide(2, 2);
  //     cBgLike->cd(1);
  //     hSigBg->DrawCopy();
  //     cBgLike->cd(2);
  //     hBgLike->DrawCopy();
  //     cBgLike->cd(3);
  //     hSigBg->DrawCopy();
  //     hBgLikeNorm->SetLineColor(kGreen);
  //     hBgLikeNorm->DrawCopy("same");
  //     cBgLike->cd(4);
  //     hPeakBgLike->DrawCopy();
  //   }
  //   if (hBgMixing) {
  //     TCanvas * cBgMixing = new TCanvas("cBgMixing", "cBgMixing");
  //     cBgMixing->Divide(2, 2);
  //     cBgMixing->cd(1);
  //     hSigBg->DrawCopy();
  //     cBgMixing->cd(2);
  //     hBgMixing->DrawCopy();
  //     cBgMixing->cd(3);
  //     hSigBg->DrawCopy();
  //     hBgMixingNorm->SetLineColor(kViolet);
  //     hBgMixingNorm->DrawCopy("same");
  //     cBgMixing->cd(4);
  //     hPeakBgMixing->DrawCopy();
  //   }

  //   if (!hBgLike && !hBgMixing) {
  //     TCanvas * cNoBg = new TCanvas("cNoBg", "cNoBg");
  //     cNoBg->Divide(2, 1);
  //     cNoBg->cd(1);
  //     hSigBg->DrawCopy();
  //     cNoBg->cd(2);
  //     hPeakNoBg->DrawCopy();
  //   }
  // }

  // outputList->Print();
  // return outputList;
  return true;
}

Double_t Pol1(double * x, double * par)
{
  return par[0] + x[0] * par[1];
}
Double_t Pol2(double * x, double * par)
{
  return par[0] + x[0] * par[1] + x[0] * x[0] * par[2];
}

Double_t VoigtPol1(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]) + Pol1(x, &par[4]);
}

Double_t VoigtPol2(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]) + Pol2(x, &par[4]);
}

Double_t GausPol2(double * x, double * par)
{
  return par[0] * TMath::Gaus(x[0], par[1], par[2]) + Pol2(x, &par[3]);
}

TList * RsnFunctions(std::string name, Double_t min, Double_t max, bool reuseFunctions)
{
  if (_currentFunctions && reuseFunctions) {
    return _currentFunctions;
  }
  if (_currentFunctions && !reuseFunctions) {
    delete _currentFunctions;
    _currentFunctions = nullptr;
  }
  _currentFunctions = new TList();

  if (name.compare("VoigtPol2") == 0) {
    TF1 * sigBgFnc = new TF1("VoigtPol2", VoigtPol2, min, max, 7);
    // set parameter names
    sigBgFnc->SetParNames("yield", "mass", "width", "sigma", "p0", "p1", "p2");
    sigBgFnc->SetNpx(1000);
    _currentFunctions->Add(sigBgFnc);

    TF1 * bgFnc = new TF1("Pol2", Pol2, min, max, 3);
    // set parameter names
    bgFnc->SetParNames("p0", "p1", "p2");
    bgFnc->SetNpx(1000);
    _currentFunctions->Add(bgFnc);
  }
  else if (name.compare("VoigtPol1") == 0) {
    TF1 * sigBgFnc = new TF1("VoigtPol1", VoigtPol1, min, max, 6);
    // set parameter names
    sigBgFnc->SetParNames("yield", "mass", "width", "sigma", "p0", "p1");
    sigBgFnc->SetNpx(1000);
    _currentFunctions->Add(sigBgFnc);

    TF1 * bgFnc = new TF1("Pol2", Pol2, min, max, 3);
    // set parameter names
    bgFnc->SetParNames("p0", "p1", "p2");
    bgFnc->SetNpx(1000);
    _currentFunctions->Add(bgFnc);
  }
  else if (name.compare("GausPol2") == 0) {
    TF1 * sigBgFnc = new TF1("GausPol2", GausPol2, min, max, 6);
    // set parameter names
    sigBgFnc->SetParNames("yield", "mass", "width", "p0", "p1", "p2");
    sigBgFnc->SetNpx(1000);
    _currentFunctions->Add(sigBgFnc);

    TF1 * bgFnc = new TF1("Pol2", Pol2, min, max, 3);
    // set parameter names
    bgFnc->SetParNames("p0", "p1", "p2");
    bgFnc->SetNpx(1000);
    _currentFunctions->Add(bgFnc);
  }
  else {
    return nullptr;
  }

  return _currentFunctions;
}
