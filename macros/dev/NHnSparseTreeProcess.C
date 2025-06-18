#include <string>
#include <vector>
#include <THnSparse.h>
#include <TROOT.h>
#include <TH1.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
#include "NDimensionalExecutor.h"
void NHnSparseTreeProcess(std::string filename = "/tmp/hnst.root", std::string enabledBranches = "unlikepm")
{
  TH1::AddDirectory(kFALSE);

  Ndmspc::NHnSparseTree * hnst = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnst == nullptr) {
    return;
  }

  std::vector<int> mins = {0};
  std::vector<int> maxs = {(int)hnst->GetEntries()};
  // mins[0]               = 0;
  // maxs[0]               = 1;

  TMacro * m =
      Ndmspc::NUtils::OpenMacro(TString::Format("%s/dev/MyProcess.C", gSystem->Getenv("NDMSPC_MACRO_DIR")).Data());
  if (m == nullptr) {
    Ndmspc::NLogger::Error("Cannot open macro MyProcess.C");
    return;
  }
  m->Load();

  Ndmspc::NDimensionalExecutor executor(mins, maxs);
  auto                         task = [hnst, m](const std::vector<int> & coords) {
    Ndmspc::NLogger::Info("Processing entry %d of %d", coords[0], hnst->GetEntries());
    hnst->GetEntry(coords[0]);
    Ndmspc::NHnSparseTreePoint * hnstPoint = hnst->GetPoint();
    // hnstPoint->Print("A");
    Longptr_t ok = gROOT->ProcessLine(
        TString::Format("%s((Ndmspc::NHnSparseTreePoint*)%p);", m->GetName(), (void *)hnstPoint).Data());
  };
  executor.Execute(task);
}
