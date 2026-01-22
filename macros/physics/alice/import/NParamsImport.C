#include <TFile.h>
#include <NLogger.h>
#include <NGnTree.h>
void NParamsImport(
    const std::string & filename = "root://eos.ndmspc.io//eos/ndmspc/scratch/veronika/PWGLF-376/pp/2024/FCPAR.root",
    std::string filenameOut = "", const std::string & hnsName = "sResults", const std::string & paramAxis = "Results")
{
  ///
  /// Macro to import parameters in THnSparse object to NGnTree
  ///

  json cfg;
  cfg["filename"]                              = filename;
  cfg["axes"]                                  = {"axis1-pt", "axis2-ce"};
  cfg["objectFormatMinMax"]                    = "%.2f_%.2f";
  cfg["objectFormatAxis"]                      = "__";
  cfg["objects"]["DataOverview"]["prefix"]     = "Data/Overview/cOverview_";
  cfg["objects"]["DataFitResults"]["prefix"]   = "Data/Fit/cFit_";
  cfg["objects"]["DataNormPlots"]["prefix"]    = "Data/Norm/cNorm_";
  cfg["objects"]["McDataOverview"]["prefix"]   = "MC/Data/Overview/cOverview_";
  cfg["objects"]["McDataFitResults"]["prefix"] = "MC/Data/Fit/cFit_";
  cfg["objects"]["McDataNormPlots"]["prefix"]  = "MC/Data/Norm/cNorm_";
  cfg["objects"]["MCTrue"]["prefix"]           = "MC/Data/True/cTrue_";
  cfg["objects"]["MCGen"]["prefix"]            = "MC/Data/Gen/cGen_";

  TFile * fIn = TFile::Open(filename.c_str());

  if (!fIn || fIn->IsZombie()) {
    NLogError("NParamsImport: Cannot open file '%s' !!!", filename.c_str());
    return;
  }

  THnSparseD * hns = (THnSparseD *)fIn->Get(hnsName.c_str());
  if (!hns) {
    NLogError("NParamsImport: Cannot get THnSparse '%s' from file '%s' !!!", hnsName.c_str(), filename.c_str());
    return;
  }

  fIn->Close();

  if (filenameOut.empty()) {
    filenameOut = filename;
    // replace extension with _ngnt.root
    filenameOut = filenameOut.substr(0, filenameOut.find_last_of('.')) + "_ngnt.root";
  }

  auto ngnt = new Ndmspc::NGnTree(hns, paramAxis, filenameOut, cfg);
  if (ngnt->IsZombie()) {
    NLogError("NParamsImport: Failed to create NGnTree for THnSparse import !!!");
    return;
  }
  ngnt->Close();
}
