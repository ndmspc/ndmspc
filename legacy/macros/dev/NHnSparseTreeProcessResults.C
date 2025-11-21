#include <NBinningDef.h>
#include <string>
#include <vector>
#include "NHnSparseTree.h"
#include "NProjection.h"
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
  std::string binningName = hnstIn->GetBinning()->GetCurrentDefinitionName();
  Ndmspc::NLogger::Info("Using binning definition: '%s'", binningName.c_str());
  Ndmspc::NBinningDef *                                def = hnstIn->GetBinning()->GetDefinition(binningName);
  std::map<std::string, std::vector<std::vector<int>>> b   = def->GetDefinition();
  // print size of b
  Ndmspc::NLogger::Info("Binning size: %zu [%s]", b.size(), binningName.c_str());
  hnstOut->ImportBinning(binningName, b);
  hnstOut->Print();

  if (hnstOut->GetBinning() == nullptr) {
    Ndmspc::NLogger::Error("Binning is not initialized in output HnSparseTree");
    return;
  }

  if (hnstOut->GetBinning()->GetContent()->GetNbins() <= 0) {
    Ndmspc::NLogger::Error("No bins in output HnSparseTree");
    return;
  }
  std::vector<int> mins = {1};
  std::vector<int> maxs = {(int)hnstIn->GetBinning()->GetContent()->GetNbins()};
  // mins[0]               = 0;
  // maxs[0] = 400;
  // maxs[0] = 1;
  //
  // hnst->Close();

  hnstOut->Process(NdmspcUserProcessResults, mins, maxs, nThreads, hnstIn, hnstOut);

  //
  Ndmspc::NProjection * hnsObj = (Ndmspc::NProjection *)hnstIn->GetPoint()->GetHnSparseObject();
  std::vector<int>      projIds(hnsObj->GetBinning()->GetAxes().size());
  std::iota(projIds.begin(), projIds.end(), 0); // Fill projIds with 0, 1, 2, ..., n-1
  std::vector<std::vector<int>> projections = Ndmspc::NUtils::Permutations(projIds);
  for (const auto & proj : projections) {
    hnsObj->Draw(proj, "");
  }

  hnstOut->Close(true);
}
