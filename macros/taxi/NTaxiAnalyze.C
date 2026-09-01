#include <string>
#include <vector>

#include <TSystem.h>
#include <TInterpreter.h>
#include <TROOT.h>
#include <TFile.h>
#include <THnSparse.h>
#include <TCanvas.h>

#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>
#include <ndmspc/core/NBinning.h>

void NTaxiAnalyze(std::string inputFile = "NTaxiConvert.root",
                  std::string outFile = "NTaxiAnalyze.root")
{


  // TObjArray *       axes            = new TObjArray();
  Ndmspc::NGnTree * ngnt_taxi_hns = Ndmspc::NGnTree::Open(inputFile.c_str());
  if (ngnt_taxi_hns->IsZombie()) {
    NLogError("NTaxiAnalyze: Failed to open NGnTree from file %s", inputFile.c_str());
    return;
  }
  Ndmspc::NGnTree * ngnt_taxi = new Ndmspc::NGnTree(ngnt_taxi_hns, outFile);
  ngnt_taxi->SetInput(ngnt_taxi_hns);

  json cfg = json::object();
  // cfg["opt"]               = "A";
  // cfg["inputPrefix"] = "/home/mvala/.ngnt_taxi/";
  // cfg["inputSuffix"] = ".parquet";
  cfg["parameters"] = {"parEntries"};

  ngnt_taxi->InitParameters({"distance_mean", "distance_rms", "distance_integral", "amount_mean", "amount_rms", "amount_integral"});

  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // NLogInfo("Thread ID: %d", threadId);

    if (!point) {
      NLogError("NTaxiAnalyze: Point is nullptr !!!");
      return;
    }
    point->Print();
    // if (point->GetEntryNumber() > 1) {
    //   NLogWarning("NTaxiAnalyze: Skipping Point ...");
    //   return;
    // }

    json cfg = point->GetCfg();

    Ndmspc::NGnTree * ngntIn = point->GetInput();
    if (ngntIn) {
      // ngntIn->Print();
      // return;
      ngntIn->GetEntry(point->GetEntryNumber());

      THnSparse * hns = (THnSparse *)(ngntIn->GetStorageTree()->GetBranchObject("hns"));
      if (hns) {
        // hns->Print();
        TH1 * h2 = hns->Projection(2);
        h2->SetName(TString::Format("taxi_%s", hns->GetAxis(2)->GetName()).Data());
        h2->SetTitle(TString::Format("%s : %s", hns->GetAxis(2)->GetName(), point->GetString().c_str()).Data());
        h2->Rebin(10);
        outputPoint->Add(h2);
        TH1 * h12 = hns->Projection(12);
        h12->SetName(TString::Format("taxi_%s", hns->GetAxis(12)->GetName()).Data());
        h12->SetTitle(TString::Format("%s : %s", hns->GetAxis(12)->GetName(), point->GetString().c_str()).Data());
        outputPoint->Add(h12);

        TH2 * h12_2 = hns->Projection(12, 2);
        h12_2->SetName(
            TString::Format("taxi_%s_vs_%s", hns->GetAxis(2)->GetName(), hns->GetAxis(12)->GetName()).Data());
        h12_2->SetTitle(TString::Format("%s vs %s : %s", hns->GetAxis(2)->GetName(), hns->GetAxis(12)->GetName(),
                                        point->GetString().c_str())
                            .Data());
        outputPoint->Add(h12_2);
        TH3 * h6_7_9 = hns->Projection(6, 7, 9);
        h6_7_9->SetName(TString::Format("taxi_%s_vs_%s_vs_%s", hns->GetAxis(6)->GetName(), hns->GetAxis(7)->GetName(),
                                        hns->GetAxis(9)->GetName())
                            .Data());
        h6_7_9->SetTitle(TString::Format("%s vs %s vs %s : %s", hns->GetAxis(6)->GetName(), hns->GetAxis(7)->GetName(),
                                         hns->GetAxis(9)->GetName(), point->GetString().c_str())
                             .Data());
        outputPoint->Add(h6_7_9);
        TH3 * h10_11_14 = hns->Projection(10, 11, 14);
        h10_11_14->SetName(TString::Format("taxi_%s_vs_%s_vs_%s", hns->GetAxis(10)->GetName(),
                                           hns->GetAxis(11)->GetName(), hns->GetAxis(14)->GetName())
                               .Data());
        h10_11_14->SetTitle(TString::Format("%s vs %s vs %s : %s", hns->GetAxis(10)->GetName(),
                                            hns->GetAxis(11)->GetName(), hns->GetAxis(14)->GetName(),
                                            point->GetString().c_str())
                                .Data());
        outputPoint->Add(h10_11_14);

        // std::vector<std::string> labels  = cfg["parameters"].get<std::vector<std::string>>();
        // TH1D *                   results = new TH1D("results", "Results", labels.size(), 0, labels.size());
        // for (size_t i = 0; i < labels.size(); i++) {
        //   results->GetXaxis()->SetBinLabel(i + 1, labels[i].c_str());
        // }
        // results->SetBinContent(1, h2->GetMean());
        // results->SetBinError(1, 0.0);
        // outputPoint->Add(results);

        Ndmspc::NParameters * params = point->GetParameters();
        params->SetParameter("distance_mean", h2->GetMean(), h2->GetMeanError());
        params->SetParameter("distance_rms", h2->GetRMS(), h2->GetRMSError());
        params->SetParameter("distance_integral", h2->Integral(),TMath::Sqrt(h2->Integral()));
        params->SetParameter("amount_mean", h12->GetMean(), h12->GetMeanError());
        params->SetParameter("amount_rms", h12->GetRMS(), h12->GetRMSError());
        params->SetParameter("amount_integral", h12->Integral(),TMath::Sqrt(h12->Integral()));

        //
        // delete hns;
      }
      else {
        NLogError("NTaxiAnalyze: Failed to retrieve THnSparse for point %s", point->GetString().c_str());
      }
    }
    else {
      NLogError("NTaxiAnalyze: Input NGnTree is nullptr for point %s", point->GetString().c_str());
    }
  };


  ngnt_taxi->Process(processFunc, cfg);

  // Clean up
  delete ngnt_taxi;
}
