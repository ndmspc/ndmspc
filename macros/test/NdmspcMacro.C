#include <TCanvas.h>
#include <TF1.h>
#include <TROOT.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TList.h>
#include <TString.h>
// #include <TStyle.h>
// #include "NdmspcUtils.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "RsnFunctions.h"

void NdmspcDefaultConfig(json & cfg)
{
  cfg = R"({
  "user": {
    "idAxis": [
      0
    ],
    "minEntries": -1,
    "norm": {
      "min": 0.99,
      "max": 1.00
    },
    "fit": {
      "fun": "VoigtPol2",
      "min": 0.997,
      "max": 1.050,
      "default": [
        0.0,
        1.019445,
        0.00426,
        0.001
      ]
    },
    "verbose": 0
  },
  "ndmspc": {
    "data": {
      "file": "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/cern.ch/user/a/alihyperloop/outputs/0013/138309/16826/AnalysisResults.root",
      "objects": ["phianalysis-t-hn-sparse_MC_analysis/unlike","phianalysis-t-hn-sparse_MC_analysis/likep+phianalysis-t-hn-sparse_MC_analysis/liken"]
    },
    "cuts": [{"enabled": true,"axis": "axis1-pt", "rebin": 2,"bin": {"min": 3 ,"max": 3}}],
    "result": {
      "names": ["RawBC", "RawFnc", "Mass", "Width", "Sigma" , "Chi2", "Probability", "True", "Gen", "Eff"]
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "out.root",
      "opt": "?remote=1"
    },
    "process": {
      "type": "single"
    },
    "verbose": 0
  }
})"_json;





}

TList * NdmspcProcess(TList * inputList, json cfg, THnSparse * finalResults, Int_t * point)
{

  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }

  if (inputList == nullptr) return nullptr;

  bool mc = false;
  if (inputList->At(2) != nullptr && inputList->At(3) != nullptr) mc = true;

  int imAxis = cfg["user"]["idAxis"][0].get<int>();

  THnSparse * sigBgSparse = (THnSparse *)inputList->At(0);
  THnSparse * bgSparse    = (THnSparse *)inputList->At(1);

  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();

  TH1 * sigBgProj = (TH1 *)sigBgSparse->Projection(imAxis);
  sigBgProj->SetNameTitle("sigBg", TString::Format("sigBg %s", titlePostfix.Data()).Data());
  // sigBgProj->SetOption("E1");

  if (sigBgProj->GetEntries() < cfg["user"]["minEntries"].get<int>()) return nullptr;

  TH1 * bgProj = (TH1 *)bgSparse->Projection(imAxis);
  bgProj->SetNameTitle("bg", TString::Format("bg %s", titlePostfix.Data()).Data());
  // bgProj->SetOption("E1");

  TH1 * bgProjNorm = (TH1 *)bgProj->Clone();
  bgProjNorm->SetNameTitle("bgNorm", TString::Format("bgNorm %s", titlePostfix.Data()).Data());
  // bgProjNorm->SetOption("E1");
  TH1 * peak = (TH1 *)sigBgProj->Clone();
  peak->SetNameTitle("peak", TString::Format("peak %s", titlePostfix.Data()).Data());
  // peak->UseCurrentStyle();
  // peak->SetOption("E1");

  Int_t binNormMin = peak->GetXaxis()->FindBin(cfg["user"]["norm"]["min"].get<double>());
  Int_t binNormMax = peak->GetXaxis()->FindBin(cfg["user"]["norm"]["max"].get<double>());
  // better norm : best for fitting is to have all points above 0(zero)

  // // Setting fitting range
  Double_t fitMin  = cfg["user"]["fit"]["min"].get<double>();
  Double_t fitMax  = cfg["user"]["fit"]["max"].get<double>();
  Int_t    bin_min = peak->GetXaxis()->FindBin(fitMin);
  Int_t    bin_max = peak->GetXaxis()->FindBin(fitMax);
  Double_t eps     = 1e-5;

  Double_t norm = sigBgProj->Integral(binNormMin, binNormMax) / bgProjNorm->Integral(binNormMin, binNormMax);
  bgProjNorm->Scale(norm); // scaling the bgProjNorm histogram
  peak->Add(bgProjNorm, -1);
  TH1 * signal         = (TH1 *)peak->Clone();
  TH1 * sigBgProjClone = (TH1 *)sigBgProj->Clone();

  // Some initial values for fitting
  Double_t       p0p       = cfg["user"]["fit"]["default"][0].get<double>();
  const Double_t phi_mass  = cfg["user"]["fit"]["default"][1].get<double>();
  const Double_t phi_width = cfg["user"]["fit"]["default"][2].get<double>();
  const Double_t phi_sigma = cfg["user"]["fit"]["default"][3].get<double>();

  TList * funs = RsnFunctions(cfg["user"]["fit"]["fun"].get<std::string>(), fitMin, fitMax);
  if (funs == nullptr) {
    Printf("Error: Functions were not found for '%s' !!! Exiting ...", cfg["user"]["fit"]["fun"].get<std::string>().c_str());
    return nullptr;
  }
  TF1 * sigBgFnc = (TF1 *)funs->At(0);
  TF1 * bgFnc    = (TF1 *)funs->At(1);

  // p0p = peak->Integral(peak->FindBin(fitMin), peak->FindBin(fitMax)) * peak->GetBinWidth(peak->FindBin(fitMin));

  // Sets init parameters for fitting
  sigBgFnc->SetParameters(p0p, phi_mass, phi_width, phi_sigma, 0.0, 0.0, 0.0, 0.0);
  // Fixing parameter for resolution
  // sigBgFnc->FixParameter(3, phi_sigma);

  // Doing fit
  Int_t nFit = 10;
  for (Int_t iFit = 0; iFit < nFit; iFit++) {
    peak->Fit(sigBgFnc, "QN MFC", "", fitMin, fitMax);
  }

  TFitResultPtr fFitResult = peak->Fit(sigBgFnc, "QN MF S", "", fitMin, fitMax);
  Double_t      par[7];
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

  Double_t integral;
  Double_t bg;
  Double_t err;
  Double_t histWidth = peak->GetXaxis()->GetBinWidth(1);
  Long64_t frbin;

  integral = peak->IntegralAndError(bin_min, bin_max, err);
  // Printf("Integral = %f +/- %f", integral, err);
  if (verbose >= 1) Printf("histWidth = %f", histWidth);

  bg = bgFnc->Integral(fitMin, fitMax, eps);
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

  if (mc) {
    THnSparse * trueSparse = (THnSparse *)inputList->At(2);
    THnSparse * genSparse  = (THnSparse *)inputList->At(3);

    hTrue = new TH1D("hTrue", "True #varphi", 1, 0., 1.);
    hGen  = new TH1D("hGen", "Gen #varphi", 1, 0., 1.);

    TH1 * trueProj = (TH1 *)trueSparse->Projection(imAxis);
    TH1 * genProj  = (TH1 *)genSparse->Projection(imAxis);

    trueIntegral = trueProj->IntegralAndError(0, -1, trueErr);
    genIntegral  = genProj->IntegralAndError(0, -1, genErr);

    hTrue->SetBinContent(1, trueIntegral);
    hTrue->SetBinError(1, trueErr);
    hGen->SetBinContent(1, genIntegral);
    hGen->SetBinError(1, genErr);

    pEff = (TH1 *)hTrue->Clone();
    pEff->Divide(hTrue, hGen, 1, 1, "b");

    // if (TEfficiency::CheckConsistency(*hTrue, *hGen)) {
    //   pEff = new TEfficiency(*hTrue, *hGen);
    //   pEff->SetNameTitle("hEff", "Efficiency #varphi");
    // } else
    //   return nullptr;
  }

  TList * outputList = new TList();
  if (finalResults) {
    outputList->Add(finalResults);
    for (auto & name : cfg["ndmspc"]["result"]["names"]) {
      std::string n = name.get<std::string>();
      if (!n.compare("RawBC")) {
        point[0] = 1;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, integralFnc);
        finalResults->SetBinError(frbin, err);
      }
      if (!n.compare("RawFnc")) {
        point[0] = 2;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, integral);
        finalResults->SetBinError(frbin, errFnc);
      }
      if (!n.compare("Mass")) {
        point[0] = 3;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetParameter(1));
        finalResults->SetBinError(frbin, sigBgFnc->GetParError(1));
      }
      if (!n.compare("Width")) {
        point[0] = 4;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetParameter(2));
        finalResults->SetBinError(frbin, sigBgFnc->GetParError(2));
      }
      if (!n.compare("Sigma")) {
        point[0] = 5;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetParameter(3));
        finalResults->SetBinError(frbin, sigBgFnc->GetParError(3));
      }
      if (!n.compare("Chi2")) {
        point[0] = 6;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetChisquare());
        finalResults->SetBinError(frbin, std::sqrt(sigBgFnc->GetChisquare()));
      }
      if (!n.compare("Probability")) {
        point[0] = 7;
        frbin    = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetProb());
        finalResults->SetBinError(frbin, std::sqrt(sigBgFnc->GetProb()));
      }
      if (mc) {
        if (!n.compare("True")) {
          point[0] = 8;
          frbin    = finalResults->GetBin(point);
          finalResults->SetBinContent(point, hTrue->GetBinContent(1));
          finalResults->SetBinError(frbin, hTrue->GetBinError(1));
        }
        if (!n.compare("Gen")) {
          point[0] = 9;
          frbin    = finalResults->GetBin(point);
          finalResults->SetBinContent(point, hGen->GetBinContent(1));
          finalResults->SetBinError(frbin, hGen->GetBinError(1));
        }
        if (!n.compare("Eff")) {
          point[0] = 10;
          frbin    = finalResults->GetBin(point);
          finalResults->SetBinContent(point, pEff->GetBinContent(1));
          finalResults->SetBinError(frbin, pEff->GetBinError(1));
        }
      }
    }
  }

  // add fit and background function to histogram so it is automatically drawn
  // with hist
  peak->GetListOfFunctions()->Add(sigBgFnc);
  peak->GetListOfFunctions()->Add(bgFnc);

  TCanvas * c = new TCanvas();
  c->Divide(2, 2);
  c->cd(1);
  sigBgProj->Draw();
  c->cd(2);
  bgProj->Draw();
  c->cd(3);
  sigBgProj->Draw();
  bgProjNorm->Draw("same");
  c->cd(4);
  peak->Draw();

  // if (mc) {
  //   TCanvas* c2 = new TCanvas();
  //   c2->Divide(2, 2);
  //   c2->cd(1);
  //   hTrue->Draw();
  //   c2->cd(2);
  //   hGen->Draw();
  //   c2->cd(3);
  //   if (pEff)
  //     pEff->Draw();
  // }

  outputList->Add(sigBgProj);
  outputList->Add(bgProj);
  outputList->Add(bgProjNorm);
  outputList->Add(peak);
  outputList->Add(c);

  if (mc) {
    outputList->Add(hTrue);
    outputList->Add(hGen);
    if (pEff) outputList->Add(pEff);
  }

  if (!gROOT->IsBatch()) {
    c->Draw();
  }

  return outputList;
}
