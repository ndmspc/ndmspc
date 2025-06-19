#include <string>
#include <vector>
#include <THnSparse.h>
#include <TROOT.h>
#include <TH1.h>
#include <TInterpreter.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
#include "NDimensionalExecutor.h"
using ProcessFuncPtr = void (*)(Ndmspc::NHnSparseTreePoint *);
#include "MyProcess.C"
// #include "PhiAnalysis.C"

void NHnSparseTreeProcess(std::string filename = "/tmp/hnst.root", std::string enabledBranches = "unlikepm,likepp")
{
  TH1::AddDirectory(kFALSE);

  Ndmspc::NHnSparseTree * hnst = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnst == nullptr) {
    return;
  }

  std::vector<int> mins = {1};
  std::vector<int> maxs = {(int)hnst->GetEntries()};
  // mins[0]               = 0;
  maxs[0] = 400;
  maxs[0] = 1;

  TMacro * m = nullptr;
  // TMacro * m =
  //     Ndmspc::NUtils::OpenMacro(TString::Format("%s/dev/MyProcess.C", gSystem->Getenv("NDMSPC_MACRO_DIR")).Data());
  // if (m == nullptr) {
  //   Ndmspc::NLogger::Error("Cannot open macro MyProcess.C");
  //   return;
  // }
  // m->Load();

  // ProcessFuncPtr greet = [](Ndmspc::NHnSparseTreePoint *) { std::cout << "Hello from a lambda!" << std::endl; };
  Ndmspc::ProcessFuncPtr myfun = MyProcess; // Use the function defined in MyProcess.C

  Ndmspc::NDimensionalExecutor executor(mins, maxs);
  auto                         task = [hnst, m, myfun](const std::vector<int> & coords) {
    Ndmspc::NLogger::Info("Processing entry %d of %d", coords[0], hnst->GetEntries());
    hnst->GetEntry(coords[0] - 1);
    Ndmspc::NHnSparseTreePoint * hnstPoint = hnst->GetPoint();
    myfun(hnstPoint, 0); // Call the lambda function
    // // hnstPoint->Print("A");
    // Longptr_t ok = gROOT->ProcessLine(
    //     TString::Format("%s((Ndmspc::NHnSparseTreePoint*)%p);", m->GetName(), (void *)hnstPoint).Data());
  };
  executor.Execute(task);
}
