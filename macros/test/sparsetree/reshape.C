#include <string>
#include <vector>
#include <TH1.h>
#include <TH2.h>
#include <TAxis.h>
#include <TCanvas.h>
#include <THnSparse.h>
#include "Logger.h"
#include "HnSparseTree.h"
#include "HnSparseTreeUtils.h"

void reshape(
    std::string filename = "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/rsn/bins/1/6/2/6/AnalysisResults.root",
    std::string objname  = "phianalysis-t-hn-sparse_default/unlikepm")
{

  // Init logger
  auto logger = Ndmspc::Logger::getInstance();
  // Ndmspc::HnSparseTree * hnst   = Ndmspc::HnSparseTree::Load(filename.c_str());
  // if (!hnst) {
  //   logger->Error("Failed to load HnSparseTree from %s", filename.c_str());
  //   return;
  // }
  // hnst->Print();
  TFile *     _file0 = TFile::Open(filename.c_str());
  THnSparse * s      = (THnSparse *)gFile->Get(objname.c_str());

  std::vector<TAxis *> newAxes;
  TAxis *              a = new TAxis(10, 0, 1);
  a->SetNameTitle("a1", "a1 title");
  newAxes.push_back(a);
  TAxis * a2 = new TAxis(100, -10, 10);
  a2->SetNameTitle("a2", "a2 title");
  newAxes.push_back(a2);
  std::vector<int> newPoint = {6, 55};
  // print newAxes
  std::vector<int> order = {4, 0, 7, 2, 3, 6, 5, 1};
  THnSparse *      s2    = Ndmspc::HnSparseTreeUtils::ReshapeSparseAxes(s, order, newAxes, newPoint, "E");
  if (s2 == nullptr) {
    logger->Error("Failed to reshape sparse axes");
    return;
  }

  //
  // THnSparse * s    = Ndmspc::HnSparseTreeUtils::ReshapeSparseAxes(hnst, order);
  TH1 * hInv  = s2->Projection(2, "O");
  TH2 * hDM   = s2->Projection(1, 2, "O");
  TH1 * hInv2 = s2->Projection(5, "O");
  TH2 * hDM2  = s2->Projection(1, 5, "O");

  TCanvas * c = new TCanvas("c", "c", 800, 600);
  c->Divide(2, 2);
  c->cd(1);
  hInv->Draw();
  c->cd(2);
  hDM->Draw("colz");
  c->cd(3);
  hInv2->Draw();
  c->cd(4);
  hDM2->Draw("colz");
}
