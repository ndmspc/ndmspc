#include <string>
#include <vector>
#include "TNamed.h"
#include "TObjArray.h"
#include "TString.h"
#include <TROOT.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NAliRsnStep1(
    std::string inFile  = "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/pp/LHC24f4d/651360/AnalysisResults.root",
    std::string outFile = "NAliRsnStep1_ngnt.root")
{

  json cfg               = json::object();
  cfg["file"]            = inFile;
  cfg["objectDirecotry"] = "phianalysis-t-hn-sparse_tpctof";
  cfg["objectNames"]     = {"unlikepm", "mixingpm", "mixingmp", "likepp", "likemm", "rotationpm", "unliketrue","unlikegen"};
  cfg["axes"]            = {"pt:axis1-pt", "ce:axis2-ce"};
  cfg["proj"]            = 0;

  TObjArray *              axes        = new TObjArray();
  std::string              objectDir   = cfg["objectDirecotry"].get<std::string>();
  std::vector<std::string> objectNames = cfg["objectNames"].get<std::vector<std::string>>();

  NLogInfo("Opening file: %s", inFile.c_str());
  TFile * f = Ndmspc::NUtils::OpenFile(inFile);
  if (!f) {
    NLogError("Failed to open file: %s", inFile.c_str());
    return;
  }

  THnSparse * hns =
      dynamic_cast<THnSparse *>(f->Get(TString::Format("%s/%s", objectDir.c_str(), objectNames[0].c_str()).Data()));
  if (!hns) {
    NLogError("Failed to get object: %s/%s from file: %s", objectDir.c_str(), objectNames[0].c_str(), inFile.c_str());
    return;
  }
  hns->Print("A");
  for (auto & axis : cfg["axes"]) {
    std::string axisStr = axis.get<std::string>();
    auto        parts   = Ndmspc::NUtils::Tokenize(axisStr, ':');
    if (parts.size() < 2) {
      parts.push_back(parts[0]);
    }

    TAxis * axisObj = dynamic_cast<TAxis *>(hns->GetListOfAxes()->FindObject(parts[1].c_str()));
    if (!axisObj) {
      NLogError("Axis not found: %s", parts[0].c_str());
      return;
    }
    axes->Add(axisObj->Clone(parts[0].c_str()));
  }

  axes->Print();

  delete hns;

  f->Close();
  delete f;

  // return;

  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  ngnt->Print();

  // Define the binning for the axes

  // std::map<std::string, std::vector<std::vector<int>>> b0;
  // b0["pt"] = {{150}};
  // b0["ce"] = {{100}};
  // ngnt->GetBinning()->AddBinningDefinition("b0", b0);

  std::map<std::string, std::vector<std::vector<int>>> b;
  b["pt"] = {{4, 1}, {1, 16}, {2, 5}, {5, 4}, {10, 1}, {20, 1}, {30, 1}};
  b["ce"] = {{1, 1}, {4, 1}, {5, 3}, {10, 3}, {20, 1}, {30}};
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // std::map<std::string, std::vector<std::vector<int>>> b2;
  // b2["pt"] = {{50}};
  // b2["ce"] = {{50}};
  // ngnt->GetBinning()->AddBinningDefinition("b2", b2);

  // std::map<std::string, std::vector<std::vector<int>>> b3;
  // b3["pt"] = {{50}};
  // // b3["ce"] = {{25}};
  // b3["ce"] = {{50,1},{25}};
  // ngnt->GetBinning()->AddBinningDefinition("b3", b3);

  // std::map<std::string, std::vector<std::vector<int>>> b4;
  // b4["pt"] = {{1}};
  // b4["ce"] = {{1}};
  // ngnt->GetBinning()->AddBinningDefinition("b4", b4);

  // std::map<std::string, std::vector<std::vector<int>>> b5;
  // b5["pt"] = {{2}};
  // b5["ce"] = {{1}};
  // ngnt->GetBinning()->AddBinningDefinition("b5", b5);

  // std::map<std::string, std::vector<std::vector<int>>> b6;
  // b6["pt"] = {{1}};
  // b6["ce"] = {{2}};
  // ngnt->GetBinning()->AddBinningDefinition("b6", b6);

  // std::map<std::string, std::vector<std::vector<int>>> b7;
  // b7["pt"] = {{2}};
  // b7["ce"] = {{2}};
  // ngnt->GetBinning()->AddBinningDefinition("b7", b7);


  // ngnt->Print();

  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                             int threadId) {
    // point->Print();
    NLogInfo("[%d] Processing point: %s", threadId, point->GetString().c_str());
    json cfg = point->GetCfg();

    std::string              filePath    = cfg["file"].get<std::string>();
    std::string              objectDir   = cfg["objectDirecotry"].get<std::string>();
    std::vector<std::string> objectNames = cfg["objectNames"].get<std::vector<std::string>>();
    int                      invmassIdx  = cfg["proj"].get<int>();

    // NLogInfo("Processing file: %s", filePath.c_str());

    TFile * f = Ndmspc::NUtils::OpenFile(filePath);
    if (!f) {
      NLogError("Failed to open file: %s", filePath.c_str());
      return;
    }

    std::map<int, std::vector<int>> ranges;
    for (size_t i = 0; i < point->GetBaseAxisRanges().size(); i++) {
      auto range = point->GetBaseAxisRanges()[i];
      // NLogDebug("Axis %d range: [%d, %d]", i + 1, range[0], range[1]);
      if (invmassIdx <= i) {
        ranges[i + 1] = range;
      }
      else {
        ranges[i] = range;
      }
    }

    for (const auto & objectName : objectNames) {
      // NLogDebug("Getting object: %s/%s from file: %s", objectDir.c_str(), objectName.c_str(), filePath.c_str());
      THnSparse * hns =
          dynamic_cast<THnSparse *>(f->Get(TString::Format("%s/%s", objectDir.c_str(), objectName.c_str()).Data()));
      if (!hns) {
        // NLogError("Failed to get object: %s/%s from file: %s", objectDir.c_str(), objectName.c_str(), filePath.c_str());
        continue;
      }

      // Ndmspc::NUtils::SetAxisRanges(hns, ranges, false, true);
      Ndmspc::NUtils::SetAxisRanges(hns, ranges);

      TH1 * proj = hns->Projection(invmassIdx);
      proj->SetName(objectName.c_str());
      proj->SetTitle(TString::Format("%s %s", objectName.c_str(), point->GetString().c_str()).Data());
      outputPoint->Add(proj);
      delete hns;
    }

    f->Close();
    delete f;
  };

  // Define the begin function which is executed before processing all points
  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    // NLogInfo("Starting processing ...");
    TH1::AddDirectory(kFALSE);
  };

  // Define the end function which is executed after processing all points
  Ndmspc::NGnEndFuncPtr endFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    // NLogInfo("Finished processing ...");
  };
  // execute the processing function
  ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);

  // Clean up
  delete ngnt;
}
