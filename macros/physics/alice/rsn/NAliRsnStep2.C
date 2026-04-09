#include <string>
#include <vector>
#include "TNamed.h"
#include "TObjArray.h"
#include "TString.h"
#include <TROOT.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NAliRsnStep2(std::string inFile = "NAliRsnStep1_ngnt.root", std::string outFile = "NAliRsnStep2_ngnt.root")
{

  json cfg = json::object();
  // cfg["file"]            = inFile;
  // cfg["objectDirecotry"] = "phianalysis-t-hn-sparse_tpctof";
  // cfg["objectNames"]     = {"unlikepm", "mixingpm",   "mixingmp",   "likepp",
  //                           "likemm",   "rotationpm", "unliketrue", "unlikegen"};
  // cfg["axes"]            = {"pt:axis1-pt", "ce:axis2-ce"};
  // cfg["proj"]            = 0;

  Ndmspc::NGnTree * ngntIn = Ndmspc::NGnTree::Open(inFile.c_str());
  if (!ngntIn || ngntIn->IsZombie()) {
    NLogError("NTaxiProcess: Failed to open NGnTree from file %s", inFile.c_str());
    return;
  }
  // ngntIn->Print();

  TObjArray * axes = new TObjArray();
  for (auto & axis : ngntIn->GetBinning()->GetAxes()) {
    NLogInfo("Cloning axis: %s", axis->GetName());
    axes->Add((TAxis *)axis->Clone());
  }

  TAxis * bg = Ndmspc::NUtils::CreateAxisFromLabels("bg", "bg", {"likepp", "likemm", "rotationpm"});
  axes->Add(bg);
  // delete ngntIn;

  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);
  ngnt->SetInput(ngntIn);

  // Define the binning for the axes

  std::map<std::string, std::vector<std::vector<int>>> b;
  b["pt"] = {{4, 1}, {1, 16}, {2, 5}, {5, 4}, {10, 1}, {20, 1}, {30, 1}};
  b["ce"] = {{1, 1}, {4, 1}, {5, 3}, {10, 3}, {20, 1}, {30}};
  // b["pt"] = {{150}};
  // b["ce"] = {{100}};

  b["bg"] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", b);
  ngnt->Print();

  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                             int threadId) {
    //   // point->Print();
    NLogInfo("[%d] Processing point: %s", threadId, point->GetString().c_str());
    //   json cfg = point->GetCfg();

    Ndmspc::NGnTree * ngntIn = point->GetInput();
    if (!ngntIn) return;
    // ngntIn->Print();
    // return;
    // ngntIn->GetEntry(point->GetEntryNumber());
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
