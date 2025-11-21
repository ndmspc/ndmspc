#include <string>
#include <vector>
#include <THnSparse.h>
#include <TH1.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
void NHnSparseTreeRead(int timeout = 100, std::string filename = "$HOME/.ndmspc/dev/hnst.root",
                       std::string enabledBranches = "unlikepm")
{
  TH1::AddDirectory(kFALSE);

  Ndmspc::NHnSparseTree * hnst = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnst == nullptr) {
    return;
  }

  hnst->Print("P");
  Ndmspc::NLogger::Info("Entries=%d", hnst->GetEntries());
  for (Long64_t i = 0; i < hnst->GetEntries(); i++) {
    hnst->GetEntry(i);
    Ndmspc::NHnSparseTreePoint * hnstPoint = hnst->GetPoint();
    // hnstPoint->Print("A");
    THnSparse * sUnlikePM = (THnSparse *)hnst->GetBranchObject("unlikepm");
    if (sUnlikePM == nullptr) {
      Ndmspc::NLogger::Error("Cannot get object 'unlikepm' from file '%s'", filename.c_str());
      return;
    }
    // Ndmspc::NLogger::Info("Point title: '%s'", hnstPoint->GetTitle("Unlike").c_str());
    sUnlikePM->SetTitle(hnstPoint->GetTitle("Unlike").c_str());
    sUnlikePM->Print();
    // Ndmspc::NLogger::Info("Point content: [%f,%f]", hnstPoint->GetPointMin(1), hnstPoint->GetPointMax(1));

    // for (int j = 0; j < hnst->GetNdimensions(); j++) {
    //   std::vector<int> bbb = hnstPoint->GetPointBinning(j);
    //   Ndmspc::NLogger::Info("Point binning: %s", Ndmspc::NUtils::GetCoordsString(bbb, -1).c_str());
    // }
  }
}
