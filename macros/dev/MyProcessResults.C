#include <THnSparse.h>
#include <TH1D.h>
#include <TF1.h>
#include <TGraphErrors.h>
#include <NLogger.h>
#include <NHnSparseTree.h>
#include <NHnSparseTreePoint.h>
#include <NHnSparseTreeThreadData.h>
#include <AnalysisFunctions.h>
#include <AnalysisUtils.h>
#include <vector>
// static std::mutex g_func_name_mutex;

Ndmspc::ProcessFuncPtr NdmspcUserProcessResults = [](Ndmspc::NHnSparseTreePoint * p, TList * output,
                                                     TList * outputGlobal, int thread_id) {
  Ndmspc::NHnSparseTree * hnst = p->GetHnSparseTree();
  // Print point
  p->Print("A");

  std::vector<int> varAxes = p->GetVariableAxisIndexes();
  Ndmspc::NLogger::Info("Variable axes: %s", Ndmspc::NUtils::GetCoordsString(varAxes, -1).c_str());

  // hnst->GetBranch("output")->Print();
  TList * lResults = (TList *)hnst->GetBranchObject("output");
  if (lResults == nullptr) {
    Ndmspc::NLogger::Error("Output list 'output' not found in NHnSparseTree !!!");
    return;
  }
  TH1D * hResults = (TH1D *)lResults->FindObject("results");
  if (hResults == nullptr) {
    Ndmspc::NLogger::Error("Histogram 'results' not found in output list !!!");
    return;
  }
  // Print all labels
  for (int i = 1; i <= hResults->GetNbinsX(); i++) {
    Ndmspc::NLogger::Info("Bin %d: %s", i, hResults->GetXaxis()->GetBinLabel(i));
  }
  // hResults->Print();
  // Print all bin contents
  TGraphErrors * massGraph = (TGraphErrors *)outputGlobal->FindObject("mass");
  if (massGraph == nullptr) {
    massGraph = new TGraphErrors();
    massGraph->SetName("mass");
    massGraph->SetMarkerStyle(20);
    massGraph->SetMarkerColor(kRed);
    massGraph->SetMarkerSize(1);
    outputGlobal->Add(massGraph);
  }
  Int_t binMass = hResults->GetXaxis()->FindBin("mass");
  Ndmspc::NLogger::Info("Bin for mass: %d", binMass);
  Double_t mass      = hResults->GetBinContent(binMass);
  Double_t massError = hResults->GetBinError(binMass);
  Double_t min       = p->GetPointMin(1);
  Double_t max       = p->GetPointMax(1);
  Ndmspc::NLogger::Info("Mass: %f, Min: %f, Max: %f center=%f", mass, min, max, (min + max) / 2);
  massGraph->SetPoint(massGraph->GetN(), (min + max) / 2, mass);
  massGraph->SetPointError(massGraph->GetN() - 1, (max - min) / 2, massError);
};
