#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>
#include <TF1.h>

#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>
#include <ndmspc/core/NSystematicsStats.h>

void NSystematics02Gaus(std::string inFile = "NSystematics01Gaus.root", std::string outFile = "NSystematics02Gaus.root")
{
  json cfg;
  cfg["inFile"] = inFile;

  cfg["parameter"]["meanFit"]["histo"]        = {100, -3.0, 3.0};
  cfg["parameter"]["sigmaFit"]["histo"]       = {100, 0.0, 5.0};
  // cfg["parameter"]["meanFit"]["histoAbsDev"]  = {100, 0.0, 100.0};
  // cfg["parameter"]["sigmaFit"]["histoAbsDev"] = {100, 0.0, 100.0};
  // cfg["parameter"]["meanFit"]["histoRelDev"]  = {100, 0.0, 1.0};
  // cfg["parameter"]["sigmaFit"]["histoRelDev"] = {100, 0.0, 1.0};
  cfg["ref"]["seed"]["bin"]                   = 2;
  cfg["ref"]["repeat"]["bin"]                 = 1;
  cfg["ref"]["entries"]["bin"]                = 5;
  cfg["sys"]["seed"]                          = {"seed"};
  cfg["sys"]["repeat"]                        = {"repeat"};
  // cfg["sys"]["seed_repeat"]         = {"seed", "repeat"};
  // cfg["sys"]["entries"]             = {"entries"};
  // cfg["sys"]["entries_repeat"]      = {"entries", "repeat"};
  // cfg["sys"]["entries_seed"]        = {"entries", "seed"};
  // cfg["sys"]["entries_seed_repeat"] = {"entries", "seed", "repeat"};

  std::vector<std::string> axisNames;
  std::vector<std::string> sysAxisNames;

  for (auto & [key, value] : cfg["sys"].items()) {
    for (auto & name : value) {
      axisNames.push_back(name.get<std::string>());
    }
    sysAxisNames.push_back(key);
  }

  // make unique
  std::sort(axisNames.begin(), axisNames.end());
  axisNames.erase(std::unique(axisNames.begin(), axisNames.end()), axisNames.end());
  cfg["axisNames"] = axisNames;

  NLogInfo("Processing systematics for axes: %s", Ndmspc::NUtils::Join(sysAxisNames, ',').c_str());
  // return;
  auto ngntIn = Ndmspc::NGnTree::Open(inFile);

  std::vector<TAxis *> axes = ngntIn->GetBinning()->GetAxes();

  // Handle merging/filtering axes
  axes.erase(std::remove_if(axes.begin(), axes.end(),
                            [&axisNames](TAxis * axis) {
                              std::string name = axis->GetName();
                              return (std::find(axisNames.begin(), axisNames.end(), name) != axisNames.end());
                            }),
             axes.end());

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  // Set binning for axis1 (rebin to 1 bin)
  for (auto & axis : axes) {
    std::string name = axis->GetName();
    b[name]          = {{1}};
  }

  // axisNames.insert(axisNames.begin(), "all");
  // add axis as first axis
  TAxis * aSys = Ndmspc::NUtils::CreateAxisFromLabels("sys", "Systematics", sysAxisNames);
  // axes.insert(axes.begin(), aSys);
  axes.push_back(aSys);
  b["sys"] = {{1}};

  // Create an NGnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);
  ngnt->Print();

  // Create the binning definition with name "default" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  ngntIn->GetEntry(0);
  Ndmspc::NParameters * pointParamsIn =
      (Ndmspc::NParameters *)ngntIn->GetStorageTree()->GetBranch("_params")->GetObject();
  NLogInfo("Parameters in the input file: %s", Ndmspc::NUtils::Join(pointParamsIn->GetNames(), ',').c_str());

  std::vector<std::string> parNames;
  for (auto & name : pointParamsIn->GetNames()) {
    NLogInfo("Adding parameter: %s", name.c_str());
    parNames.push_back(std::string(name) + "Mean");
    parNames.push_back(std::string(name) + "MeanAbsDev");
    parNames.push_back(std::string(name) + "MeanRelAbsDev");
    parNames.push_back(std::string(name) + "MaxAbsDev");
    parNames.push_back(std::string(name) + "MaxRelAbsDev");
  }
  cfg["parameters"] = pointParamsIn->GetNames();

  ngnt->InitParameters(parNames);

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int threadId) {
    point->Print("S");
    point->Print("C");
    // Retrieve configuration
    json cfg = point->GetCfg();

    auto ngntIn = (Ndmspc::NGnTree *)point->GetTempObject("inFile");
    if (!ngntIn || ngntIn->IsZombie()) {
      ngntIn = Ndmspc::NGnTree::Open(cfg["inFile"].get<std::string>(), "_params");
      point->SetTempObject("inFile", ngntIn);
    }

    auto hnsIn = ngntIn->GetBinning()->GetContent();

    Long64_t                      linBin = 0;
    std::vector<std::vector<int>> rangesTmp;
    Int_t *                       c = point->GetCoords();

    int axisIndex;
    for (auto axis : point->GetBinning()->GetAxes()) {
      std::string name = axis->GetName();
      if (name.compare("sys") == 0) {
        std::string              sysLabel  = point->GetBinLabel(name);
        std::vector<std::string> axisNames = cfg["axisNames"].get<std::vector<std::string>>();
        // remove all elements in cfg["sys"][sysLabel] from cfg["axisNames"]
        for (auto & n : cfg["sys"][sysLabel]) {
          std::string axisName = n.get<std::string>();
          auto        it       = std::find(axisNames.begin(), axisNames.end(), axisName);
          if (it != axisNames.end()) {
            axisNames.erase(it);
          }
        }

        // print the remaining axis names
        NLogDebug("Remaining axis names: %s", Ndmspc::NUtils::Join(axisNames, ',').c_str());

        for (auto & axisName : axisNames) {
          int iAxis = ngntIn->GetBinning()->GetAxisIndex(axisName);
          axisIndex = 3 * iAxis;

          int idx = cfg["ref"][axisName]["bin"].get<int>();
          rangesTmp.push_back({axisIndex, 1, 1});
          rangesTmp.push_back({axisIndex + 1, 1, 1});
          rangesTmp.push_back({axisIndex + 2, idx, idx});
          NLogDebug("Processing axis: %s (%s), index: %d, coords: %d, %d, %d", axisName.c_str(), sysLabel.c_str(),
                    axisIndex, 1, 1, idx);
        }
      }
      else {
        int iAxis = point->GetBinning()->GetAxisIndex(name) * 3;
        axisIndex = 3 * ngntIn->GetBinning()->GetAxisIndex(name);
        rangesTmp.push_back({axisIndex, c[iAxis], c[iAxis]});
        rangesTmp.push_back({axisIndex + 1, c[iAxis + 1], c[iAxis + 1]});
        rangesTmp.push_back({axisIndex + 2, c[iAxis + 2], c[iAxis + 2]});
        // NLogDebug("Processing axis: %s, index: %d, coords: %d, %d, %d", name.c_str(), axisIndex, c[axisIndex],
        //           c[axisIndex + 1], c[axisIndex + 2]);
      }
    }

    Ndmspc::NUtils::SetAxisRanges(hnsIn, rangesTmp); // Set the ranges for the axes
    std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{hnsIn->CreateIter(true /*use axis range*/)};
    std::vector<int>                                linBins;

    std::map<std::string, Ndmspc::NSystematicsStats> statsMap;
    std::vector<std::string>                         parameters = cfg["parameters"].get<std::vector<std::string>>();
    for (auto & parName : parameters) {
      statsMap[parName] = Ndmspc::NSystematicsStats(parName.c_str(), TString::Format("%s %s", parName.c_str(),point->GetString().c_str()).Data(),
                                                    (int)cfg["parameter"][parName]["histo"][0].get<double>(),
                                                    cfg["parameter"][parName]["histo"][1].get<double>(),
                                                    cfg["parameter"][parName]["histo"][2].get<double>());
    }
    while ((linBin = iter->Next()) >= 0) {
      ngntIn->GetEntry(linBin);
      ngntIn->GetBinning()->GetPoint()->Print("S");
      bool isReference = true;
      for (auto & n : cfg["sys"][point->GetBinLabel("sys")]) {
        int idx = cfg["ref"][n.get<std::string>()]["bin"].get<int>();

        if (ngntIn->GetBinning()->GetPoint()->GetBin(n.get<std::string>()) != idx) {
          isReference = false;
          break;
        }
      }

      NLogDebug("Processing linBin %lld, isReference: %s", linBin, isReference ? "true" : "false");
      Ndmspc::NParameters * pointParamsIn =
          (Ndmspc::NParameters *)ngntIn->GetStorageTree()->GetBranch("_params")->GetObject();
      if (pointParamsIn) {
        pointParamsIn->Print();
        for (int bin = 1; bin <= pointParamsIn->GetHisto()->GetNbinsX(); bin++) {
          std::string parName = pointParamsIn->GetHisto()->GetXaxis()->GetBinLabel(bin);
          double      value   = pointParamsIn->GetHisto()->GetBinContent(bin);
          double      error   = pointParamsIn->GetHisto()->GetBinError(bin);
          if (isReference) {
            statsMap[parName].SetReference(value, error, true);
          }
          else {
            statsMap[parName].AddMeasurement(value, error);
          }
        }
      }
    }
    for (auto & parName : parameters) {
      statsMap[parName].Compute();
    }

    Ndmspc::NParameters * pointParams = point->GetParameters();
    if (pointParams) {

      for (auto & parName : parameters) {
        pointParams->SetParameter((parName + "Mean").c_str(), statsMap[parName].GetMean(kTRUE),
                                  statsMap[parName].GetMeanStdError(kTRUE));
        pointParams->SetParameter((parName + "MeanAbsDev").c_str(),
                                  statsMap[parName].GetMeanAbsDeviationFromReference());
        pointParams->SetParameter((parName + "MaxAbsDev").c_str(), statsMap[parName].GetMaxAbsDeviationFromReference());

        pointParams->SetParameter((parName + "MeanRelAbsDev").c_str(),
                                  statsMap[parName].GetMeanRelDeviationFromReference());
        pointParams->SetParameter((parName + "MaxRelAbsDev").c_str(),
                                  statsMap[parName].GetMaxRelDeviationFromReference());

        NLogInfo(
            "%s: mean = %f, meanStdError = %f, meanAbsDev = %f, maxAbsDev = %f, meanRelAbsDev = %f, maxRelAbsDev = %f",
            parName.c_str(), statsMap[parName].GetMean(kTRUE), statsMap[parName].GetMeanStdError(kTRUE),
            statsMap[parName].GetMeanAbsDeviationFromReference(), statsMap[parName].GetMaxAbsDeviationFromReference(),
            statsMap[parName].GetMeanRelDeviationFromReference(), statsMap[parName].GetMaxRelDeviationFromReference());
        outputPoint->Add(statsMap[parName].GetHisto());
        outputPoint->Add(statsMap[parName].GetDevHisto());
        outputPoint->Add(statsMap[parName].GetRelDevHisto());
      }
    }

    outputPoint->Add(new TH1D("hInfo", TString::Format("h_%s", point->GetString().c_str()).Data(), 2, 0, 2));
    // // outputPoint->Add(c);
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
