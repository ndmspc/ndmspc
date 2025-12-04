#include <string>
#include <vector>
#include "TDirectoryFile.h"
#include "TNamed.h"
#include "TObjArray.h"
#include "TString.h"
#include <TROOT.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NAliRsnResults(int nThreads = 1, std::string outFile = "/tmp/AliRsnResults_ngnt.root")
{
  // Enable multithreading if nThreads > 1
  if (nThreads != 1) ROOT::EnableImplicitMT(nThreads);

  json cfg               = json::object();
  cfg["basePath"]        = "root://eos.ndmspc.io//eos/ndmspc/scratch/veronika/Phi/outputs";
  cfg["fcpar"]           = "FCPAR.root";
  cfg["cpar"]            = "CPAR.root";
  cfg["par"]             = "PAR.root";
  cfg["objectDirecotry"] = "phianalysis-t-hn-sparse_default";
  cfg["objectNames"]     = {"unlikepm", "mixingpm", "likepp", "likemm"};

  std::string              basePath      = cfg["basePath"].get<std::string>();
  std::string              fcparFileName = cfg["fcpar"].get<std::string>();
  std::vector<std::string> paths         = Ndmspc::NUtils::Find(basePath, fcparFileName);

  std::vector<std::string>                     axesNames = {"collision", "year"};
  std::map<std::string, std::set<std::string>> axes;

  for (const auto & path : paths) {
    Ndmspc::NLogger::Info("Found file: %s", path.c_str());
    // remove prefix basePath from path
    TString relativePath = path;
    relativePath.ReplaceAll(basePath.c_str(), "");
    relativePath.ReplaceAll(fcparFileName.c_str(), "");
    relativePath.ReplaceAll("years", "");
    relativePath.ReplaceAll("data", "");
    relativePath.ReplaceAll("//", "/");
    // remove leading slash
    relativePath.Remove(0, relativePath.BeginsWith("/") ? 1 : 0);
    // remove trailing slash
    relativePath.Remove(relativePath.EndsWith("/") ? relativePath.Length() - 1 : relativePath.Length(), 1);

    std::vector<std::string> tokens = Ndmspc::NUtils::Tokenize(relativePath.Data(), '/');

    // if (tokens.size() < axesNames.size()) {
    //   tokens.push_back("mb");
    // }
    //
    if (tokens.size() != axesNames.size()) {
      continue;
    }

    for (size_t i = 0; i < tokens.size(); ++i) {
      axes[axesNames[i]].insert(tokens[i]);
    }
  }

  TObjArray * axesArr = new TObjArray();
  for (const auto & axisName : axesNames) {
    TAxis * axis = Ndmspc::NUtils::CreateAxisFromLabelsSet(axisName, axisName, axes[axisName]); // Convert set to vector
    axesArr->Add(axis);
  }

  // Create an NHnSparseObject from the THnSparse
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axesArr, outFile);

  std::map<std::string, std::vector<std::vector<int>>> b;

  // add binning definition from all axes
  for (const auto & axisName : axesNames) {
    b[axisName] = {{1}};
  }

  ngnt->GetBinning()->AddBinningDefinition("default", b);
  ngnt->Print();

  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // Ndmspc::NLogger::Info("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    point->Print();
    json        cfg           = point->GetCfg();
    std::string basePath      = cfg["basePath"].get<std::string>();
    std::string fcparFileName = cfg["fcpar"].get<std::string>();
    std::string cparFileName  = cfg["cpar"].get<std::string>();
    std::string parFileName   = cfg["par"].get<std::string>();
    std::string filePath      = basePath;
    // append slash if not present
    if (filePath.back() != '/') filePath += "/";
    filePath += point->GetLabels()[0] + "/";
    filePath += "years/";
    filePath += point->GetLabels()[1] + "/";
    filePath += "data/";

    std::string fcparPath = filePath + fcparFileName;

    Ndmspc::NLogger::Info("Processing file: %s", fcparPath.c_str());
    //
    TFile * fileFCPAR = Ndmspc::NUtils::OpenFile(fcparPath, "READ", false);
    if (!fileFCPAR || fileFCPAR->IsZombie()) {
      Ndmspc::NLogger::Error("Failed to open file: %s", fcparPath.c_str());
      return;
    }

    // std::string correctedPath = "Corrected/hCorrected";
    TDirectoryFile * correctedList = fileFCPAR->Get<TDirectoryFile>("Corrected");
    // loop over all keys in the directory
    for (const auto & key : *correctedList->GetListOfKeys()) {
      std::string keyName    = key->GetName();
      TH1 *       hCorrected = correctedList->Get<TH1>(keyName.c_str()); // get histogram by key name from the directory
      if (!hCorrected) {
        Ndmspc::NLogger::Error("Failed to get histogram: %s from file: %s", keyName.c_str(), fcparPath.c_str());
        continue;
      }
      hCorrected->SetDirectory(nullptr); // Detach histogram from file
      outputPoint->Add(hCorrected);
    }
    TDirectoryFile * originalList = fileFCPAR->Get<TDirectoryFile>("Original");
    // loop over all keys in the directory
    for (const auto & key : *originalList->GetListOfKeys()) {
      std::string keyName   = key->GetName();
      TH1 *       hOriginal = originalList->Get<TH1>(keyName.c_str()); // get histogram by key name from the directory
      if (!hOriginal) {
        Ndmspc::NLogger::Error("Failed to get histogram: %s from file: %s", keyName.c_str(), fcparPath.c_str());
        continue;
      }
      hOriginal->SetDirectory(nullptr); // Detach histogram from file
      outputPoint->Add(hOriginal);
    }

    fileFCPAR->Close();
  };

  // return;
  bool rc = ngnt->Process(processFunc, cfg);
  // // ngnt_taxi->Print();
  //
  if (rc) {
    Ndmspc::NLogger::Info("NAliRsnDownload: Processing completed successfully.");
    ngnt->Close(true);
  }
  else {
    Ndmspc::NLogger::Error("NAliRsnDownload: Processing failed.");
    ngnt->Close(false);
  }

  // Clean up
  delete ngnt;
}
