#include <TCanvas.h>
#include <TF1.h>
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

void NdmspcDefaultConfig(json& cfg)
{
  cfg = R"({
  "idAxis": [0],
  "minEntries": -1,
  "norm": {"min": 0.99,"max": 1.00},
  "fit": {"fun": "VoigtPol2","min": 0.997,"max": 1.050, "default": [0.0, 1.019445, 0.00426, 0.001]},
  "ndmspc": {
    "data": {
      "file": "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/cern.ch/user/a/alihyperloop/outputs/0013/138309/16826/AnalysisResults.root",
      "objects": ["phianalysis-t-hn-sparse_MC_analysis/unlike","phianalysis-t-hn-sparse_MC_analysis/likep+phianalysis-t-hn-sparse_MC_analysis/liken"]
    },
    "cuts": [{"enabled": true,"axis": "axis1-pt","bin": {"min": 3 ,"max": 3, "rebin": 2}}],
    "result": {
      "names": ["RawBC", "RawFnc", "Mass", "Width", "Sigma" , "Chi2", "Probability"]
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
  },
  "verbose" : 0
})"_json;
}

TList* NdmspcProcess(
  TList* inputList,
  json cfg,
  THnSparse* finalResults,
  Int_t* point)
{

  int verbose = 0;
  if (!cfg["verbose"].is_null() && cfg["verbose"].is_number_integer()) {
    verbose = cfg["verbose"].get<int>();
  }

  if (inputList == nullptr)
    return nullptr;

  TList* outputList = new TList();

  // gStyle->SetMarkerStyle(20);
  // gStyle->SetOptStat(11111111);
  // gStyle->SetOptFit(1111);

  THnSparse* sigBgSparse = (THnSparse*)inputList->At(0);
  THnSparse* bgSparse = (THnSparse*)inputList->At(1);

  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();

  TH1* sigBgProj = (TH1*)sigBgSparse->Projection(cfg["idAxis"][0].get<int>());
  sigBgProj->SetNameTitle("sigBg", TString::Format("sigBg %s", titlePostfix.Data()).Data());
  // sigBgProj->SetOption("E1");

  if (sigBgProj->GetEntries() < cfg["minEntries"].get<int>())
    return nullptr;

  TH1* bgProj = (TH1*)bgSparse->Projection(cfg["idAxis"][0].get<int>());
  bgProj->SetNameTitle("bg", TString::Format("bg %s", titlePostfix.Data()).Data());
  // bgProj->SetOption("E1");
  TH1* bgProjNorm = (TH1*)bgProj->Clone();
  bgProjNorm->SetNameTitle("bgNorm", TString::Format("bgNorm %s", titlePostfix.Data()).Data());
  // bgProjNorm->SetOption("E1");
  TH1* peak = (TH1*)sigBgProj->Clone();
  peak->SetNameTitle("peak", TString::Format("peak %s", titlePostfix.Data()).Data());
  // peak->UseCurrentStyle();
  // peak->SetOption("E1");

  Int_t binNormMin = peak->GetXaxis()->FindBin(cfg["norm"]["min"].get<double>());
  Int_t binNormMax = peak->GetXaxis()->FindBin(cfg["norm"]["max"].get<double>());
  // better norm : best for fitting is to have all points above 0(zero)

  // // Setting fitting range
  Double_t fitMin = cfg["fit"]["min"].get<double>();
  Double_t fitMax = cfg["fit"]["max"].get<double>();
  Int_t bin_min = peak->GetXaxis()->FindBin(fitMin);
  Int_t bin_max = peak->GetXaxis()->FindBin(fitMax);
  Double_t eps = 1e-5;

  Double_t norm = sigBgProj->Integral(binNormMin, binNormMax) / bgProjNorm->Integral(binNormMin, binNormMax);
  bgProjNorm->Scale(norm); // scaling the bgProjNorm histogram
  peak->Add(bgProjNorm, -1);
  TH1* signal = (TH1*)peak->Clone();
  TH1* sigBgProjClone = (TH1*)sigBgProj->Clone();

  // Some initial values for fitting
  Double_t p0p = cfg["fit"]["default"][0].get<double>();
  const Double_t phi_mass = cfg["fit"]["default"][1].get<double>();
  const Double_t phi_width = cfg["fit"]["default"][2].get<double>();
  const Double_t phi_sigma = cfg["fit"]["default"][3].get<double>();

  TList* funs = RsnFunctions(cfg["fit"]["fun"].get<std::string>(), fitMin, fitMax);
  if (funs == nullptr) {
    Printf("Error: Functions were not found for '%s' !!! Exiting ...", cfg["fit"]["fun"].get<std::string>().c_str());
    return nullptr;
  }
  TF1* sigBgFnc = (TF1*)funs->At(0);
  TF1* bgFnc = (TF1*)funs->At(1);

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
  Double_t par[7];
  sigBgFnc->GetParameters(par);
  const Double_t* parErr = sigBgFnc->GetParErrors();
  TFitResult* ftr = (TFitResult*)fFitResult.Get()->Clone();
  // Double_t errFnc = sigBgFnc->IntegralError(
  // fitMin, fitMax, fFitResult->GetParams(),
  // fFitResult->GetCovarianceMatrix().GetMatrixArray(), 1e-5);
  Double_t integralFnc = sigBgFnc->Integral(fitMin, fitMax, eps);
  Double_t errFnc = sigBgFnc->IntegralError(
    fitMin, fitMax, ftr->GetParams(),
    ftr->GetCovarianceMatrix().GetMatrixArray(), 1e-5);

  if (verbose >= 1)
    Printf("Function error %f", errFnc);

  // Getting pol2 part from fit and set it to bgFnc for drawing and then
  // subtracting
  bgFnc->SetParameters(&par[4]);

  if (verbose >= 1)
    ftr->Print("V");

  Double_t integral;
  Double_t bg;
  Double_t err;
  Double_t histWidth = peak->GetXaxis()->GetBinWidth(1);
  Long64_t frbin;

  integral = peak->IntegralAndError(bin_min, bin_max, err);
  // Printf("Integral = %f +/- %f", integral, err);
  if (verbose >= 1)
    Printf("histWidth = %f", histWidth);

  bg = bgFnc->Integral(fitMin, fitMax, eps);
  // Printf("Background = %f", bg);

  bg /= histWidth;
  if (verbose >= 1)
    Printf("Background/width = %f", bg);
  integral -= bg;
  if (verbose >= 1)
    Printf("signal = %f", integral);

  // TF1* sigBgFnc = (TF1*)peak->GetListOfFunctions()->At(0);

  // To do : Calculate errFnc error corectly, when dividing by histWidth
  if (verbose >= 1)
    Printf("integral fnc = %f+/- %f", integralFnc, errFnc);
  integralFnc /= histWidth;
  errFnc /= histWidth;

  integralFnc -= bg;
  if (verbose >= 1)
    Printf("signal fnc = %f", integralFnc);

  if (finalResults) {
    outputList->Add(finalResults);

    for (auto& name : cfg["ndmspc"]["result"]["names"]) {
      std::string n = name.get<std::string>();
      // Printf("[0]=%d [1]=%d [2]=%d", point[0], point[1], point[2]);
      if (!n.compare("RawBC")) {
        point[0] = 1;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, integralFnc);
        finalResults->SetBinError(frbin, err);
      }
      if (!n.compare("RawFnc")) {
        point[0] = 2;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, integral);
        finalResults->SetBinError(frbin, errFnc);
      }
      if (!n.compare("Mass")) {
        point[0] = 3;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetParameter(1));
        finalResults->SetBinError(frbin, sigBgFnc->GetParError(1));
      }
      if (!n.compare("Width")) {
        point[0] = 4;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetParameter(2));
        finalResults->SetBinError(frbin, sigBgFnc->GetParError(2));
      }
      if (!n.compare("Sigma")) {
        point[0] = 5;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetParameter(3));
        finalResults->SetBinError(frbin, sigBgFnc->GetParError(3));
      }
      if (!n.compare("Chi2")) {
        point[0] = 6;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetChisquare());
      }
      if (!n.compare("Probability")) {
        point[0] = 7;
        frbin = finalResults->GetBin(point);
        finalResults->SetBinContent(point, sigBgFnc->GetProb());
      }
    }
  }

  // add fit and background function to histogram so it is automatically drawn
  // with hist
  peak->GetListOfFunctions()->Add(sigBgFnc);
  peak->GetListOfFunctions()->Add(bgFnc);

  // TPaveText* pt = new TPaveText(0.65, 0.4, 0.75, 0.5, "blNDC");
  // TText* pt_LaTex = pt->AddText("p-p, #sqrt{s_{NN}} = 13.6 TeV");
  // pt->SetBorderSize(0);
  // pt->SetFillColor(0);
  // pt->SetFillStyle(0);
  // pt->SetTextFont(42);
  // pt->SetTextSize(0.05);
  // pt_LaTex = pt->AddText("#varphi #rightarrow K^{+} + K^{-}");

  TCanvas* c = new TCanvas();
  c->Divide(2, 2);
  c->cd(1);
  // sigBgProj->SetStats(0);
  // sigBgProj->SetNameTitle("sigBgProj", "Signal + Background");
  // sigBgProj->GetXaxis()->SetTitle("M_{KK} (GeV/c^{2})");
  // sigBgProj->GetYaxis()->SetTitle("N");
  // sigBgProj->SetMarkerSize(0.8);
  // sigBgProj->SetMarkerStyle(20);
  // sigBgProj->SetMarkerColor(1);
  // sigBgProj->SetMarkerColor(kBlue);
  sigBgProj->Draw();
  // pt->Draw();
  c->cd(2);
  // bgProj->SetStats(0);
  // bgProj->SetNameTitle("bgProj", "Background");
  // bgProj->GetXaxis()->SetTitle("M_{KK} (GeV/c^{2})");
  // bgProj->GetYaxis()->SetTitle("N");
  // bgProj->SetMarkerSize(0.8);
  // bgProj->SetMarkerStyle(20);
  // bgProj->SetMarkerColor(1);
  // bgProj->SetMarkerColor(kGreen);
  bgProj->Draw();
  // pt->Draw();
  c->cd(3);
  // bgProjNorm->SetStats(0);
  // bgProjNorm->SetNameTitle("bgProjNorm", "Background Norm");
  // bgProjNorm->GetXaxis()->SetTitle("M_{KK} (GeV/c^{2})");
  // bgProjNorm->GetYaxis()->SetTitle("N");
  // bgProjNorm->SetMarkerSize(0.8);
  // bgProjNorm->SetMarkerStyle(20);
  // bgProjNorm->SetMarkerColor(kGreen);
  // // sigBgProjClone->SetNameTitle("sigBgProjClone", "Signal and Background");
  // // sigBgProjClone->SetStats(0);
  // // sigBgProjClone->GetXaxis()->SetTitle("M_{KK} (GeV/c^{2})");
  // // sigBgProjClone->GetYaxis()->SetTitle("N");
  // // sigBgProjClone->SetMarkerSize(0.8);
  // // sigBgProjClone->SetMarkerStyle(20);
  // // sigBgProjClone->SetMarkerColor(kBlue);
  sigBgProj->Draw();
  bgProjNorm->Draw("same");
  // pt->Draw();
  c->cd(4);
  // peak->SetStats(0);
  // peak->SetNameTitle("peak", "Signal with fit");
  // peak->GetXaxis()->SetTitle("M_{KK} (GeV)");
  // peak->GetYaxis()->SetTitle("N");
  // peak->SetMarkerSize(0.8);
  // peak->SetMarkerStyle(20);
  // peak->SetMarkerColor(1);
  peak->Draw();
  //gStyle->SetOptFit(10111);
  // pt->Draw();
  // // // c->cd(4);
  // // // signal->SetStats(0);
  // // // signal->SetNameTitle("peak", "Signal");
  // // // signal->GetXaxis()->SetTitle("M_{KK} (GeV)");
  // // // signal->GetYaxis()->SetTitle("N");
  // // // signal->SetMarkerSize(0.8);
  // // // signal->SetMarkerStyle(20);
  // // // signal->SetMarkerColor(1);
  // // // signal->Draw();
  // // // pt->Draw();

  outputList->Add(sigBgProj);
  outputList->Add(bgProj);
  outputList->Add(bgProjNorm);
  outputList->Add(peak);
  outputList->Add(c);

  if (!gROOT->IsBatch()) {
    c->Draw();
  }

  return outputList;
}
