#include <string>
#include <vector>
#include <THnSparse.h>
#include <TH1.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
void NHnSparseTreeRead(int timeout = 100, std::string filename = "$HOME/.ndmspc/dev/ngnt.root",
                       std::string enabledBranches = "unlikepm")
{
  TH1::AddDirectory(kFALSE);

  Ndmspc::NHnSparseTree * ngnt = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (ngnt == nullptr) {
    return;
  }

  ngnt->Print("P");
  NLogInfo("Entries=%d", ngnt->GetEntries());
  for (Long64_t i = 0; i < ngnt->GetEntries(); i++) {
    ngnt->GetEntry(i);
    Ndmspc::NHnSparseTreePoint * hnstPoint = ngnt->GetPoint();
    // hnstPoint->Print("A");
    THnSparse * sUnlikePM = (THnSparse *)ngnt->GetBranchObject("unlikepm");
    if (sUnlikePM == nullptr) {
      NLogError("Cannot get object 'unlikepm' from file '%s'", filename.c_str());
      return;
    }
    // NLogInfo("Point title: '%s'", hnstPoint->GetTitle("Unlike").c_str());
    sUnlikePM->SetTitle(hnstPoint->GetTitle("Unlike").c_str());
    sUnlikePM->Print();
    // NLogInfo("Point content: [%f,%f]", hnstPoint->GetPointMin(1), hnstPoint->GetPointMax(1));

    // for (int j = 0; j < ngnt->GetNdimensions(); j++) {
    //   std::vector<int> bbb = hnstPoint->GetPointBinning(j);
    //   NLogInfo("Point binning: %s", Ndmspc::NUtils::GetCoordsString(bbb, -1).c_str());
    // }
  }
}
