#include <string>
#include <vector>
#include "TNamed.h"
#include "TObjArray.h"
#include "TString.h"
#include <TROOT.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NAliRsnDownload(int nThreads = 1, std::string outFile = "/tmp/NAliRsnDownload_ngnt.root")
{
  // Enable multithreading if nThreads > 1
  if (nThreads != 1) {
    ROOT::EnableImplicitMT(nThreads);
  }
  json cfg                   = json::object();
  cfg["basePath"]            = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/dev/alice/hyperloop/";
  cfg["analysisResultsFile"] = "AnalysisResults.root";
  cfg["objectDirecotry"]     = "phianalysis-t-hn-sparse_default";
  cfg["objectNames"]         = {"unlikepm", "mixingpm", "likepp", "likemm"};
  // // cfg["opt"]               = "A";

  std::string              basePath                = cfg["basePath"].get<std::string>();
  std::string              analysisResultsFileName = cfg["analysisResultsFile"].get<std::string>();
  std::vector<std::string> paths                   = Ndmspc::NUtils::Find(basePath, analysisResultsFileName);

  std::vector<std::string>                     axesNames = {"analysis", "id"};
  std::map<std::string, std::set<std::string>> axes;
  for (const auto & path : paths) {
    NLogInfo("Found file: %s", path.c_str());
    // remove the base path from the path
    // remove the analysisResultsFileName from the path
    std::string relativePath = path;
    // append slash if not present
    if (basePath.back() != '/') basePath += "/";
    relativePath.erase(0, basePath.size());
    relativePath.erase(relativePath.size() - analysisResultsFileName.size(), analysisResultsFileName.size());
    NLogInfo("Found file: %s", relativePath.c_str());
    std::vector<std::string> tokens = Ndmspc::NUtils::Tokenize(relativePath, '/');
    axes["analysis"].insert(tokens[0]);
    axes["id"].insert(tokens[1]);
  }

  // print axes
  TObjArray * axesArr = new TObjArray();
  for (const auto & axisName : axesNames) {
    NLogInfo("Axis: %s", axisName.c_str());
    TAxis * axis = Ndmspc::NUtils::CreateAxisFromLabelsSet(axisName, axisName, axes[axisName]); // Convert set to vector
    axesArr->Add(axis);
    axis->Print("all");
  }

  // Create an NHnSparseObject from the THnSparse
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axesArr, outFile);

  // Create brach for each objectName
  for (const auto & objectName : cfg["objectNames"]) {
    ngnt->GetStorageTree()->AddBranch(objectName.get<std::string>(), nullptr, "THnSparseD");
  }

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  b["id"] = {{1}};

  ngnt->GetBinning()->AddBinningDefinition("default", b);
  ngnt->Print();

  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // NLogInfo("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    point->Print();
    json        cfg                 = point->GetCfg();
    std::string basePath            = cfg["basePath"].get<std::string>();
    std::string analysisResultsFile = cfg["analysisResultsFile"].get<std::string>();
    std::string filePath            = basePath;
    // append slash if not present
    if (filePath.back() != '/') filePath += "/";
    filePath += point->GetLabels()[0] + "/";
    filePath += point->GetLabels()[1] + "/";
    filePath += analysisResultsFile;
    NLogInfo("Processing file: %s", filePath.c_str());

    TFile * f = Ndmspc::NUtils::OpenFile(filePath);
    if (!f) {
      NLogError("Failed to open file: %s", filePath.c_str());
      return;
    }

    std::string              objectDir   = cfg["objectDirecotry"].get<std::string>();
    std::vector<std::string> objectNames = cfg["objectNames"].get<std::vector<std::string>>();
    for (const auto & objectName : objectNames) {
      NLogInfo("Getting object: %s/%s from file: %s", objectDir.c_str(), objectName.c_str(),
                            filePath.c_str());
      THnSparse * hns =
          dynamic_cast<THnSparse *>(f->Get(TString::Format("%s/%s", objectDir.c_str(), objectName.c_str()).Data()));
      if (!hns) {
        NLogError("Failed to get object: %s/%s from file: %s", objectDir.c_str(), objectName.c_str(),
                               filePath.c_str());

        point->GetTreeStorage()->GetBranch(objectName)->SetAddress(nullptr, true);
        return;
      }
      hns->Print("");
      point->GetTreeStorage()->GetBranch(objectName)->SetAddress(hns, true);
      // outputPoint->Add(new TNamed(objectName.c_str(), filePath.c_str()));
      outputPoint->Add(hns->Projection(0));
    }
  };

  // return;
  bool rc = ngnt->Process(processFunc, cfg);
  // // ngnt_taxi->Print();
  //
  if (rc) {
    NLogInfo("NAliRsnDownload: Processing completed successfully.");
    ngnt->Close(true);
  }
  else {
    NLogError("NAliRsnDownload: Processing failed.");
    ngnt->Close(false);
  }

  // Clean up
  delete ngnt;
}
