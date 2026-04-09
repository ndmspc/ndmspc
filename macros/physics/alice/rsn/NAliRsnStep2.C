#include <string>
#include <vector>
#include "TNamed.h"
#include "TObjArray.h"
#include "TString.h"
#include <TROOT.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>
#include <AnalysisUtils.h>
#include <AnalysisFunctions.h>

void NAliRsnStep2(std::string inFile = "NAliRsnStep1_ngnt.root", std::string outFile = "NAliRsnStep2_ngnt.root")
{

  json cfg = json::object();
  cfg["norm"]["min"] = 1.05;
  cfg["norm"]["max"] = 1.07;
  cfg["parameters"] = {"yield", "mean", "width", "sigma", "p0", "p1", "p2"};
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

  TAxis * bg = Ndmspc::NUtils::CreateAxisFromLabels(
      "bg", "bg", {"mixingpm", "mixingmp", "likepp", "likemm", "rotationpm"});
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


  ngnt->InitParameters(cfg["parameters"].get<std::vector<std::string>>());

  ngnt->Print();

  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                             int threadId) {
    //   // point->Print();
    NLogInfo("[%d] Processing point: %s", threadId, point->GetString().c_str());
    json cfg = point->GetCfg();

    Ndmspc::NGnTree * ngntIn = point->GetInput();
    if (!ngntIn) return;
    // ngntIn->Print();
    // return;

    // std::vector<int> coords = point->GetCoords().data();
    Long64_t entry = ngntIn->GetBinning()->GetContent()->GetBinContent(point->GetCoords());
    NLogInfo("[%d] Getting entry %lld for point: %s", threadId, entry, point->GetString().c_str());
    ngntIn->GetEntry(entry);

    TList * inputPoint = (TList *)ngntIn->GetStorageTree()->GetBranchObject("_outputPoint");
    if (!inputPoint) {
      NLogError("[%d] No _outputPoint branch found for point: %s", threadId, point->GetString().c_str());
      return;
    }
    TH1 * hSigBg = (TH1 *)inputPoint->FindObject("unlikepm")->Clone("hSigBg");
    if (!hSigBg) {
      NLogError("[%d] No 'unlikepm' histogram found in _outputPoint for point: %s", threadId,
                point->GetString().c_str());
      return;
    }
    hSigBg->SetTitle(point->GetString().c_str());


    std::string bgName = point->GetBinLabel("bg");
    NLogInfo("[%d] Looking for background histogram with name '%s' for point: %s", threadId, bgName.c_str(),
             point->GetString().c_str());
    TH1 * hBg = (TH1 *)inputPoint->FindObject(bgName.c_str())->Clone("hBg");
    if (!hBg) {
      NLogError("[%d] No background histogram with name '%s' found in _outputPoint for point: %s", threadId,
                bgName.c_str(), point->GetString().c_str());
      delete hSigBg;
      return;
    }
    hBg->SetTitle(point->GetString().c_str());

    TF1 * fVoigtPol2 = Ndmspc::AnalysisFunctions::VoigtPol2("fVoigtPol2", 0.998, 1.042);
    fVoigtPol2->SetParameters(0.0, 1.019461, 0.00426, 0.0008, 0.0, 0.0, 0.0);

    bool accepted = Ndmspc::AnalysisUtils::ExtractSignal(hSigBg, hBg, fVoigtPol2, cfg, outputPoint, point->GetParameters()->GetHisto());
    if (!accepted) {
      NLogWarning("[%d] Point did not pass signal extraction criteria: %s", threadId, point->GetString().c_str());
      outputPoint->Delete();
      return;
    }
    // hSigBg->Add(hBg, -1);
    // hSigBg->Print();
    // outputPoint->Add(hBg);
    // outputPoint->Add(hSigBg);
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
