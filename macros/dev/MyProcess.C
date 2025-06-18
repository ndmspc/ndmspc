#include <THnSparse.h>
#include <NLogger.h>
#include <NHnSparseTree.h>
#include <NHnSparseTreePoint.h>
void MyProcess(Ndmspc::NHnSparseTreePoint * p)
{

  // Print point
  p->Print("A");

  // Access object
  Ndmspc::NHnSparseTree * hnst      = p->GetHnSparseTree();
  THnSparse *             sUnlikePM = (THnSparse *)hnst->GetBranchObject("unlikepm");
  if (sUnlikePM == nullptr) {
    Ndmspc::NLogger::Error("Cannot get object 'unlikepm'");
    return;
  }
  // Ndmspc::NLogger::Info("Point title: '%s'", p->GetTitle("Unlike").c_str());
  sUnlikePM->SetTitle(p->GetTitle("Unlike").c_str());
  sUnlikePM->Print();
}
