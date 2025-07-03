#include <THnSparse.h>
#include <TH1D.h>
#include <TF1.h>
#include <NLogger.h>
#include <NHnSparseTree.h>
#include <NHnSparseTreePoint.h>
#include <NHnSparseTreeThreadData.h>
#include <AnalysisFunctions.h>
#include <AnalysisUtils.h>
#include <vector>
// static std::mutex g_func_name_mutex;

Ndmspc::ProcessFuncPtr NdmspcUserProcess = [](Ndmspc::NHnSparseTreePoint * p, TList * output, TList * outputGlobal,
                                              int thread_id) {
  json cfg;
  cfg["norm"]["min"]   = 1.050;
  cfg["norm"]["max"]   = 1.060;
  cfg["fit"]["name"]   = "VoigtPol2";
  cfg["fit"]["min"]    = 1.000;
  cfg["fit"]["max"]    = 1.040;
  cfg["fit"]["params"] = {1.0, 1.019, 0.0045, 0.001, 0.0, 0.0, 0.0};
  cfg["parameters"]    = {"integral", "mass", "sigma", "width"};

  std::vector<std::string> labels  = cfg["parameters"].get<std::vector<std::string>>();
  TH1D *                   results = new TH1D("results", "Results", labels.size(), 0, labels.size());
  for (size_t i = 0; i < labels.size(); i++) {
    results->GetXaxis()->SetBinLabel(i + 1, labels[i].c_str());
  }

  // Print point
  // p->Print("A");

  Ndmspc::NHnSparseTree * hnst = p->GetHnSparseTree();

  TH1D * hSigBg = hnst->ProjectionFromObject("unlikepm", 0);
  if (!hSigBg) {
    Ndmspc::NLogger::Error("Histogram 'unlikepm' not found !!!");
    return;
  }
  hSigBg->SetTitle(p->GetTitle("Signal Background").c_str());

  TH1D * hBg = hnst->ProjectionFromObject("mixingpm", 0);
  if (!hBg) {
    Ndmspc::NLogger::Error("Histogram 'mixingpm' not found !!!");
    return;
  }
  hBg->SetTitle(p->GetTitle("Background").c_str());

  std::string fitFunction_name = cfg["fit"]["name"].get<std::string>();
  Double_t    phi_mass         = cfg["fit"]["params"][1].get<double>(); // Initial mass
  Double_t    phi_width        = cfg["fit"]["params"][2].get<double>(); // Initial width
  Double_t    phi_sigma        = cfg["fit"]["params"][3].get<double>(); // Initial sigma
  Double_t    fit_min          = cfg["fit"]["min"].get<double>();
  Double_t    fit_max          = cfg["fit"]["max"].get<double>();
  TF1 *       peakFunc         = Ndmspc::AnalysisFunctions::VoigtPol2(fitFunction_name.c_str(), fit_min, fit_max);
  peakFunc->SetParameters(0, phi_mass, phi_width, phi_sigma, 0.0, 0.0, 0.0);
  peakFunc->FixParameter(3, phi_sigma); // Fix sigma parameter

  Ndmspc::AnalysisUtils::ExtractSignal(hSigBg, hBg, peakFunc, cfg, output, results);

  // delete peakFunc;
};
