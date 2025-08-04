#include <THnSparse.h>
#include <TH1D.h>
#include <TF1.h>
#include <TGraphErrors.h>
#include <NLogger.h>
#include <NHnSparseObject.h>
#include <NProjection.h>
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
  // p->Print("A");

  Ndmspc::NHnSparseObject * hnsObj = p->GetHnSparseObject();
  if (hnsObj == nullptr) {
    std::vector<TAxis *> axes = hnst->GetBinning()->GetAxesByType(Ndmspc::AxisType::kVariable);
    hnsObj                    = new Ndmspc::NProjection(axes);
    p->SetHnSparseObject(hnsObj);
    // Ndmspc::NLogger::Error("HnSparseObject is not set in NHnSparseTreePoint !!!");
    // return;
  }
  // hnsObj->Print();

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
    Ndmspc::NLogger::Debug("Bin %d: %s", i, hResults->GetXaxis()->GetBinLabel(i));
  }

  hnsObj->Fill(hResults, p);
  hnsObj->Draw("colz text");
};
