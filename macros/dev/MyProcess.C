#include <THnSparse.h>
#include <TH1D.h>
#include <TF1.h>
#include <NLogger.h>
#include <NHnSparseTree.h>
#include <NHnSparseTreePoint.h>
#include <NHnSparseTreeThreadData.h>
#include "TROOT.h"
static std::mutex g_func_name_mutex;

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

Double_t BreitWigner(double * x, double * par)
{
  return par[0] * TMath::BreitWigner(x[0], par[1], par[2]);
}
Double_t Voigt(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]);
}

Ndmspc::ProcessFuncPtr NdmspcUserProcess = [](Ndmspc::NHnSparseTreePoint * p, TList * output, int thread_id) {
  // Print point
  // p->Print("A");

  // Access object
  Ndmspc::NHnSparseTree * hnst      = p->GetHnSparseTree();
  THnSparse *             sUnlikePM = (THnSparse *)hnst->GetBranchObject("unlikepm");
  if (sUnlikePM == nullptr) {
    Ndmspc::NLogger::Error("Cannot get object 'unlikepm'");
    return;
  }
  // Ndmspc::NLogger::Info("Point title: '%s'", p->GetTitle("Unlike").c_str());
  sUnlikePM->SetTitle(p->GetTitle("Unlike").c_str());
  // sUnlikePM->Print();
  TH1D * hist = sUnlikePM->Projection(0);
  // hist->Print();

  std::string name;
  TF1 *       func = nullptr;
  {

    std::lock_guard<std::mutex> lock(g_func_name_mutex); // Protect the counter
    // name+= std::to_string(thread_id);
    // Ndmspc::NLogger::Error("Fitting histogram '%s' with thread %d", hist->GetName(), thread_id);
    Double_t phi_mass  = 1.019; // Initial mass
    Double_t phi_width = 0.004; // Initial width
    Double_t phi_sigma = 0.001; // Initial sigma

    func = new TF1("VoigtPol2", VoigtPol2, 0.997, 1.050, 7);
    func->SetParameters(hist->GetMaximum(), phi_mass, phi_width, phi_sigma, 0.0, 0.0, 0.0);
    func->FixParameter(3, phi_sigma); // Fix sigma parameter
    hist->Fit(func, "RQN0");          // "S" for fit statistics, "Q" for quiet
  }
  // fit 10 times
  int nFits = 10;
  for (int i = 0; i < nFits; ++i) {
    // Ndmspc::NLogger::Info("Fit iteration %d", i);
    hist->Fit(func, "RQN0"); // "S" for fit statistics, "Q" for quiet
  }
  hist->GetListOfFunctions()->Add(func); // Add the function to the histogram's list of functions
  if (!gROOT->IsBatch()) {
    int status = hist->Fit(func, "RQ"); // "S" for fit statistics, "Q" for quiet
    Ndmspc::NLogger::Info("Fit status: %d", status);
    hist->DrawCopy("hist");
  }

  // Storing histogram in output list
  output->Add(hist); // Add histogram to output list
  // output->Add(func); // Add histogram to output list
  //
  // delete func;
};
