#include <string>
#include <THnSparse.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TCanvas.h>
#include <TVirtualPad.h>
#include "NLogger.h"
#include "NConfig.h"
#include "NHnSparseTree.h"
void Read(std::string config = "PWGLF-376-dev.json", std::string enabledBranches = "unlikepm")
{
  TH1::AddDirectory(kFALSE);
  Ndmspc::NConfig * cfg = Ndmspc::NConfig::Instance(config);
  cfg->SetEnvironment("local");
  cfg->Print();
  std::string hnstFileNameOut = cfg->GetAnalysisPath() + "/hnst.root";

  Ndmspc::NHnSparseTree * hnstOutTest = Ndmspc::NHnSparseTree::Open(hnstFileNameOut.c_str(), enabledBranches);
  if (hnstOutTest == nullptr) {
    return;
  }

  hnstOutTest->Print();
  TCanvas * c = new TCanvas("c", "c", 800, 600);
  c->Divide(2, 1);
  auto h = hnstOutTest->Projection(1, 2, 3);
  gSystem->ProcessEvents();
  h->SetMinimum(0);

  c->cd(1);
  h->DrawCopy("colz");
  gPad->ModifiedUpdate();
  gSystem->ProcessEvents();

  THnSparse * sUnlikePM = (THnSparse *)hnstOutTest->GetBranchObject("unlikepm");
  if (sUnlikePM == nullptr) {
    Ndmspc::NLogger::Error("Cannot get object 'unlikepm' from file '%s'", hnstFileNameOut.c_str());
    return;
  }
  c->cd(2);
  for (Long64_t i = 0; i < hnstOutTest->GetEntries(); i++) {
    hnstOutTest->GetEntry(i);
    sUnlikePM->Print();
    TH1 * hUnlikePM = sUnlikePM->Projection(0);
    hUnlikePM->SetMinimum(0);
    hUnlikePM->Draw("colz");
    //
    // c->ModifiedUpdate();
    gPad->ModifiedUpdate();
    gSystem->ProcessEvents();
    // gSystem->Sleep(1000);
  }
}
