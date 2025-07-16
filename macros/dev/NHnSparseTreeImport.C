#include <vector>
#include <string>
#include <NHnSparseTree.h>
void NHnSparseTreeImport(
    std::string filename =
        "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/hyperloop/PWGLF-376/360353/AnalysisResults.root",
    std::string              dir      = "phianalysis-t-hn-sparse_default",
    std::vector<std::string> objNames = {"unlikepm", "likepp", "likemm", "mixingpm"})
{

  Ndmspc::NHnSparseTree * hnst = new Ndmspc::NHnSparseTreeC("$HOME/.ndmspc/dev/hnst.root", "hnst");
  std::map<std::string, std::vector<std::vector<int>>> b;
  // b["axis1-pt"] = {{4, 1}, {1, 46}, {2, 15}, {5, 2}, {10, 1}, {20, 1}, {30, 1}, {50, 1}};
  b["axis1-pt"] = {{10}};
  b["axis2-ce"] = {{10}};

  // b["axis1-pt"] = {{5, 1}, {1, 15}, {2, 20}, {5, 10}, {10, 9}};
  // b["axis2-mu"] = {{5, 10}};
  // b["axis1-pt"] = {{5, 2}};
  // b["axis1-pt"] = {{1}};
  // b["axis1-pt"] = {{10}};
  // b["axis1-pt"] = {{50}};
  // b["axis2-ce"] = {{50}};
  // b["axis2-ce"]  = {{10}};
  // b["axis5-eta"] = {{4, 2}, {1, 1}, {4, 2}};
  // b["axis5-eta"] = {{5}};
  hnst->Import(filename, dir, objNames, b);
  hnst->Close(true);
}
