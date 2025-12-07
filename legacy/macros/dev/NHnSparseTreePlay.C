#include <string>
#include <vector>
#include <THnSparse.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TCanvas.h>
#include <TVirtualPad.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
void NHnSparseTreePlay(int timeout = 100, std::string filename = "$HOME/.ndmspc/dev/ngnt.root",
                       std::string enabledBranches = "unlikepm")
{
  TH1::AddDirectory(kFALSE);

  Ndmspc::NHnSparseTree * ngnt = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (ngnt == nullptr) {
    return;
  }

  ngnt->Print("P");

  // return;
  TCanvas * c = new TCanvas("c", "c", 800, 600);
  c->Divide(2, 1);
  auto h = ngnt->Projection(1, 2, 5);
  h->Reset();
  h->SetStats(kFALSE);
  h->SetMinimum(0);
  c->cd(1);
  h->Draw("colz");
  gPad->ModifiedUpdate();
  gSystem->ProcessEvents();

  THnSparse * sUnlikePM = (THnSparse *)ngnt->GetBranchObject("unlikepm");
  if (sUnlikePM == nullptr) {
    NLogError("Cannot get object 'unlikepm' from file '%s'", filename.c_str());
    return;
  }
  std::vector<double> min(ngnt->GetNdimensions());
  std::vector<double> max(ngnt->GetNdimensions());
  c->cd(2);
  for (Long64_t i = 0; i < ngnt->GetEntries(); i++) {
    NLogInfo("Processing entry %lld of %lld", i, ngnt->GetEntries());
    ngnt->GetEntry(i);
    Ndmspc::NHnSparseTreePoint * hnstPoint = ngnt->GetPoint();
    // hnstPoint->Print("A");

    std::vector<int> point = hnstPoint->GetPointStorage();
    // Get min and max for current point

    // std::vector<int> point = ngnt->GetPointVector();
    ngnt->GetPointMinMax(point, min, max);

    // Print current point
    std::string pointStr = ngnt->GetPointStr(point);
    NLogInfo("Current point: %s [%f,%f]", pointStr.c_str(), min[1], max[1]);
    // sUnlikePM->Print();
    TH1 * hUnlikePM = sUnlikePM->Projection(0);
    h->SetBinContent(point[1], point[2], point[5], hUnlikePM->Integral());
    hUnlikePM->SetNameTitle("hUnlike", hnstPoint->GetTitle("Unlike").c_str());
    hUnlikePM->SetMinimum(0);
    c->cd(1);
    h->Draw("colz");
    c->cd(2);
    hUnlikePM->Draw("hist");
    gPad->ModifiedUpdate();
    if (timeout > 0) gSystem->Sleep(timeout);
    gSystem->ProcessEvents();
  }
}
