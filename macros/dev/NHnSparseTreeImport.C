#include <vector>
#include <string>
#include <NHnSparseTree.h>
void NHnSparseTreeImport(std::string filename = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/dev/alice/inputs/"
                                                "PWGLF-376/0/LHC23k2f/214402/AnalysisResults.root",
                         std::string dir      = "phianalysis-t-hn-sparse_default",
                         std::vector<std::string> objNames = {"unlikepm", "likepp", "likemm"})
{
  Ndmspc::NHnSparseTree *                              hnst = new Ndmspc::NHnSparseTreeC("/tmp/hnst.root", "hnst");
  std::map<std::string, std::vector<std::vector<int>>> b;
  b["axis1-pt"] = {{5, 1}, {1, 15}, {2, 20}, {5, 10}, {10, 9}};
  // b["axis2-mu"]  = {{5, 10}};
  // b["axis1-pt"]  = {{10}};
  b["axis2-mu"]  = {{10}};
  b["axis5-eta"] = {{2}};
  hnst->Import(filename, dir, objNames, b);
  hnst->Close("true");
}
