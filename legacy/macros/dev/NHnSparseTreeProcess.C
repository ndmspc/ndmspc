#include <string>
#include <vector>
#include "NHnSparseTree.h"
#include "MyProcess.C"

void NHnSparseTreeProcess(int nThreads = 1, std::string filename = "$HOME/.ndmspc/dev/ngnt.root",
                          std::string enabledBranches = "unlikepm,mixingpm")
{

  TH1D * h = new TH1D("h", "Test Histogram", 20, -10, 10);
  h->FillRandom("gaus", 1000);
  TF1 * f1 = new TF1("f1", "gaus", 0, 10);
  h->Fit(f1, "QN");

  Ndmspc::NHnSparseTree * hnstIn = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnstIn == nullptr) {
    return;
  }

  Ndmspc::NHnSparseTree * hnstOut = new Ndmspc::NHnSparseTreeC("$HOME/.ndmspc/dev/hnst_out.root");
  if (hnstOut == nullptr) {
    NLogError("Cannot create output HnSparseTree");
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
  Ndmspc::NBinningDef * def = hnstIn->GetBinning()->GetDefinition();
  if (!def) {
    NLogError("Binning definition is nullptr in input HnSparseTree");
    return;
  }
  std::map<std::string, std::vector<std::vector<int>>> b = def->GetDefinition();
  // print size of b
  NLogInfo("Binning size: %zu", b.size());

  if (!hnstOut->ImportBinning("default", b)) {
    NLogError("Cannot import binning to output HnSparseTree");
    return;
  }
  hnstOut->Print();
  // return;

  if (hnstOut->GetBinning() == nullptr) {
    NLogError("Binning is not initialized in output HnSparseTree");
    return;
  }

  if (hnstOut->GetBinning()->GetContent()->GetNbins() <= 0) {
    NLogError("No bins in output HnSparseTree");
    return;
  }
  std::vector<int> mins = {1};
  std::vector<int> maxs = {(int)hnstOut->GetBinning()->GetContent()->GetNbins()};
  // mins[0]               = 0;
  // maxs[0] = 400;
  // maxs[0] = 1;
  //
  // ngnt->Close();

  hnstOut->Process(NdmspcUserProcess, mins, maxs, nThreads, hnstIn, hnstOut);
  hnstOut->Close(true);
}
