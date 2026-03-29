#include <string>
#include <vector>
#include <TFile.h>
#include <NLogger.h>
#include <NGnTree.h>

void NParamsImport(bool dryrun = false,
           const std::string & filename =
             "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/demo/import/FCPAR.root",
           std::string filenameOut = "/tmp/FCPAR_ngnt.root", const std::string & hnsName = "sResults",
           const std::string & paramAxis = "Results")
{
  ///
  /// Macro to import parameters in THnSparse object to NGnTree
  ///

  if (!dryrun) gSystem->Setenv("NDMSPC_LOG_CONSOLE", "0");

  json cfg;
  cfg["filename"]                   = filename;
  cfg["axes"] = json::array();
  cfg["axes"].push_back({{"name", "axis2-ce"}, {"mode", "minmax"}, {"format", "%.2f_%.2f"}, {"sufix", "/"}});
  cfg["axes"].push_back({{"name", "axis1-pt"}, {"mode", "minmax"}, {"format", "%.2f_%.2f"}, {"sufix", "/"}});

  // pass dryrun option to NGnTree import (when true it will only log constructed paths)
  cfg["dryrun"] = dryrun;

  // in short, the above is equivalent to above
  // cfg["axes"].push_back({{"name", "axis2-ce"}});
  // cfg["axes"].push_back({{"name", "axis1-pt"}});
  // cfg["axisObjectDefaultFormat"] = "%.2f_%.2f";
  // cfg["axisDefaultSeparator"]   = "/";

  // define objects to import with their path prefix and sufix (suffix can be used to distinguish different objects for same bin, e.g. data vs MC)
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

  // if running in dryrun mode, the NGnTree will not actually retrieve objects but only log the constructed paths, so we can close it immediately
  if (dryrun) {
    NLogInfo("------------------------------------------------------------------------------");
    NLogInfo("NParamsImport: Running in dryrun mode, NGnTree will only store parameters and log constructed paths without retrieving objects ...");
    NLogInfo("NParamsImport: To actually retrieve objects, set dryrun to false.");
    NLogInfo("------------------------------------------------------------------------------");
  }
}
