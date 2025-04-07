#include <string>
#include <TH1.h>
#include <TCanvas.h>
#include "Logger.h"
#include "HnSparseTree.h"
#include "Axes.h"
void axes(int         timeout = 0,
          std::string filename =
              "$HOME/.ndmspc/test/1/pt/10/0/1/2/3/4-130-1/5-200-1/6-10-1/7/8/9-8-1/1/6/2/6/1/1/1/1/1/1/hnst.root",
          std::string branches = "", int branchId = 0)
{
  // Init logger
  auto                   logger = Ndmspc::Logger::getInstance();
  Ndmspc::HnSparseTree * hnst   = Ndmspc::HnSparseTree::Load(filename);
  if (!hnst) {
    logger->Error("Failed to load HnSparseTree from %s", filename.c_str());
    return;
  }

  Ndmspc::Axes axes = hnst->GetAxes();
  axes.Print();
  /// print Path
  // Int_t point[] = {7, 1, 6, 6, 1, 1, 1, 1, 1, 1};

  // logger->Info("Path: %s", axes.GetPath(point).c_str());
  // hnst->Play(timeout, {{5, 100, 110}}, "");
  hnst->Play(timeout, {{5, 10, 10}, {6, 4, 4}}, branches, branchId);
  // TCanvas * c = new TCanvas("c", "c", 800, 600);
  // hnst->Projection(5, "O")->Draw();
  hnst->Close();
}
