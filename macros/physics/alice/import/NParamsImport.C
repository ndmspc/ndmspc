#include <string>
#include <vector>
#include <TFile.h>
#include <NLogger.h>
#include <NGnTree.h>

void NParamsImport(const std::string & filename =
                       "root://eos.ndmspc.io//eos/ndmspc/scratch/veronika/test/PWGLF-376/pp/LHC24_pass1/FCPAR.root",
                   std::string filenameOut = "", const std::string & hnsName = "sResults",
                   const std::string & paramAxis = "Results")
{
  ///
  /// Macro to import parameters in THnSparse object to NGnTree
  ///

  json cfg;
  cfg["filename"]                   = filename;
  cfg["axes"]                       = {"axis2-ce", "axis1-pt"};
  cfg["objectFormatMinMax"]         = "%.2f_%.2f";
  cfg["objectFormatAxis"]           = "/";

  std::vector<std::string> objectNames = {"hSignal","hBg","hBgNorm","hPeak","cOverview","cFit","cNorm"};
  for (const auto & objName : objectNames) {
    cfg["objects"][objName]["prefix"] = "Data/";
    cfg["objects"][objName]["sufix"]  = "/" + objName;
    cfg["objects"][objName+"MC"]["prefix"] = "MC/Data/";
    cfg["objects"][objName+"MC"]["sufix"]    = "/" + objName;
  }

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
  // ngnt->Print();
  // ngnt->Close();
}
