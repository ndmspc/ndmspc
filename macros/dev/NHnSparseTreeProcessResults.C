#include <string>
#include <vector>
#include "NHnSparseTree.h"
#include "MyProcessResults.C"

void NHnSparseTreeProcessResults(int nThreads = 1, std::string filename = "$HOME/.ndmspc/dev/hnst_out.root",
                                 std::string enabledBranches = "")
{

  Ndmspc::NHnSparseTree * hnstIn = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnstIn == nullptr) {
    return;
  }
  // hnstIn->Print("P");
  // return;

  Ndmspc::NHnSparseTree * hnstOut = new Ndmspc::NHnSparseTreeC("$HOME/.ndmspc/dev/hnst_out_results.root");
  if (hnstOut == nullptr) {
    Ndmspc::NLogger::Error("Cannot create output HnSparseTree");
    return;
  }

  std::vector<TAxis *> axesIn  = hnstIn->GetBinning()->GetAxes();
  TObjArray *          axesOut = new TObjArray();
  for (auto & axis : axesIn) {
    TAxis * axisOut = (TAxis *)axis->Clone();
    axesOut->Add(axisOut);
  }
  hnstOut->InitAxes(axesOut);

  // Get binning definition from input HnSparseTree
  std::map<std::string, std::vector<std::vector<int>>> b = hnstIn->GetBinning()->GetDefinition();
  // print size of b
  Ndmspc::NLogger::Info("Binning size: %zu", b.size());

  hnstOut->ImportBinning(b);
  hnstOut->Print();
  // return;

  if (hnstOut->GetBinning() == nullptr) {
    Ndmspc::NLogger::Error("Binning is not initialized in output HnSparseTree");
    return;
  }

  if (hnstOut->GetBinning()->GetContent()->GetNbins() <= 0) {
    Ndmspc::NLogger::Error("No bins in output HnSparseTree");
    return;
  }
  std::vector<int> mins = {1};
  std::vector<int> maxs = {(int)hnstOut->GetBinning()->GetContent()->GetNbins()};
  // mins[0]               = 0;
  // maxs[0] = 400;
  // maxs[0] = 1;
  //
  // hnst->Close();

  hnstOut->Process(NdmspcUserProcessResults, mins, maxs, nThreads, hnstIn, hnstOut);
  hnstOut->Close(true);
}
