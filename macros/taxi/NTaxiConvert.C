#include <string>
#include <vector>
#include <NBinning.h>
#include <NStorageTree.h>
#include <TSystem.h>
#include <TInterpreter.h>
#include <TROOT.h>
#include <TFile.h>
#include <THnSparse.h>
#include <TCanvas.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>
#include <NTaxi.h>
THnSparse * CreateTaxiTHnSparse();
void        NTaxiConvert(std::string inputFile = "NTaxiDownload.root",
                         std::string outFile = "NTaxiConvert.root")
{

  // TObjArray *       axes            = new TObjArray();
  Ndmspc::NGnTree * ngnt_taxi_files = Ndmspc::NGnTree::Open(inputFile.c_str());
  if (!ngnt_taxi_files || ngnt_taxi_files->IsZombie()) {
    NLogError("NTaxiProcess: Failed to open NGnTree from file %s", inputFile.c_str());
    return;
  }

  Ndmspc::NGnTree * ngnt_taxi = new Ndmspc::NGnTree(ngnt_taxi_files, outFile);
  THnSparse *       hns       = nullptr;
  ngnt_taxi->GetStorageTree()->AddBranch("hns", &hns, "THnSparseD");

  json cfg = json::object();
  // cfg["opt"]               = "A";
  cfg["inputPrefix"] = "$HOME/.ngnt_taxi/";
  cfg["inputSuffix"] = ".parquet";

  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // NLogInfo("Thread ID: %d", threadId);
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
    point->Print();
    json cfg = point->GetCfg();

    std::string fileTypeYearMonth =
        cfg["inputPrefix"].get<std::string>() + point->GetLabels()[2] + "_tripdata_" + point->GetLabels()[0] + "-" +
        TString::Format("%02d", point->GetStorageCoords()[1]).Data() + cfg["inputSuffix"].get<std::string>();
    NLogDebug("NTaxiConvert: Processing point '%s' file '%s'", point->GetString().c_str(),
                           fileTypeYearMonth.c_str());

    fileTypeYearMonth = gSystem->ExpandPathName(fileTypeYearMonth.c_str());

    if (Ndmspc::NUtils::AccessPathName(fileTypeYearMonth.c_str()) == false) {
      NLogWarning("NTaxiConvert: File %s does not exist. Skipping point ...", fileTypeYearMonth.c_str());
      return;
    }

    THnSparse * hns = Ndmspc::NTaxi::CreateSparseFromParquetTaxi(fileTypeYearMonth.c_str(), CreateTaxiTHnSparse(), -1);
    if (hns) {
      hns->SetName("hns_taxi");
      hns->SetTitle(TString::Format("THnSparse Taxi Data: %s", point->GetString().c_str()).Data());
      // hns->Print();
      TH1::AddDirectory(kFALSE);
      outputPoint->Add(new TH1S("filename", fileTypeYearMonth.c_str(), 1, 0, 1));
      Ndmspc::NStorageTree * ts = point->GetTreeStorage();
      if (ts && ts->GetBranch("hns"))
        ts->GetBranch("hns")->SetAddress(hns, true);
      else {
        NLogError("NTaxiConvert: TreeStorage is nullptr or hns object is missing !!!");
      }
      // delete hns;
    }
    else {
      NLogError("NTaxiConvert: Failed to create THnSparse for point %s", fileTypeYearMonth.c_str());
    }
  };

  bool rc = false;
  rc      = ngnt_taxi->Process(processFunc, cfg);
  // ngnt_taxi->Print();

  if (rc) {
    NLogInfo("NTaxiConvert: Processing completed successfully.");
    ngnt_taxi->Close(true);
  }
  else {
    NLogError("NTaxiConvert: Processing failed.");
    ngnt_taxi->Close(false);
  }

  // Clean up
  delete ngnt_taxi;
}

THnSparse * CreateTaxiTHnSparse()
{

  std::vector<std::string>                column_names = {"VendorID",
                                                          "passenger_count",
                                                          "trip_distance",
                                                          "RatecodeID",
                                                          "store_and_fwd_flag",
                                                          "payment_type",
                                                          "fare_amount",
                                                          "extra",
                                                          "mta_tax",
                                                          "tip_amount",
                                                          "tolls_amount",
                                                          "improvement_surcharge",
                                                          "total_amount",
                                                          "congestion_surcharge",
                                                          "Airport_fee",
                                                          "cbd_congestion_fee"};
  std::map<std::string, std::vector<int>> column_bins;
  column_bins["VendorID"]              = {7, 0, 7};
  column_bins["passenger_count"]       = {10};
  column_bins["RatecodeID"]            = {6, 1, 7};
  column_bins["store_and_fwd_flag"]    = {2};
  column_bins["trip_distance"]         = {1000, 0, 100};
  column_bins["payment_type"]          = {7, 0, 7};
  column_bins["fare_amount"]           = {100};
  column_bins["extra"]                 = {20};
  column_bins["mta_tax"]               = {10};
  column_bins["tip_amount"]            = {100};
  column_bins["tolls_amount"]          = {50};
  column_bins["improvement_surcharge"] = {10};
  column_bins["total_amount"]          = {1000};
  column_bins["congestion_surcharge"]  = {10};
  column_bins["Airport_fee"]           = {10};
  column_bins["cbd_congestion_fee"]    = {10};

  // loop over collumns_names and print their binning
  for (const auto & colName : column_names) {
    NLogTrace("Column: %s, Bins: %s", colName.c_str(),
                           Ndmspc::NUtils::Join(column_bins[colName], ',').c_str());
    // std::cout << "Column: " << colName << ", Bins: " << Ndmspc::NUtils::Join(column_bins[colName], ',') << std::endl;
  }

  // Create lists to hold axis information
  // Int_t    nDims = (Int_t)column_names.size();
  const Int_t nDims = 16;
  Int_t       nbins[nDims];
  Double_t    xmin[nDims];
  Double_t    xmax[nDims];

  for (int i = 0; i < nDims; ++i) {
    nbins[i] = column_bins[column_names[i]][0];
    xmin[i]  = column_bins[column_names[i]].size() > 1 ? column_bins[column_names[i]][1] * 1.0 : 0.0;
    xmax[i]  = column_bins[column_names[i]].size() > 2 ? column_bins[column_names[i]][2] * 1.0
                                                       : column_bins[column_names[i]][0] * 1.0;
  }

  THnSparseD *      hns = new THnSparseD("sparse_from_parquet", "Sparse from Parquet", nDims, nbins, xmin, xmax);
  std::vector<bool> isCategorical(nDims, false);
  TObjArray *       axes = hns->GetListOfAxes();
  for (int i = 0; i < axes->GetEntries(); ++i) {
    hns->GetAxis(i)->SetName(column_names[i].c_str());
    hns->GetAxis(i)->SetTitle(column_names[i].c_str());
  }
  // set labels for categorical axes
  for (int i = 0; i < nDims; ++i) {
    if (column_names[i] == "VendorID") {
      isCategorical[i] = true;
      hns->GetAxis(i)->SetBinLabel(1, "Creative Mobile Technologies, LLC");
      hns->GetAxis(i)->SetBinLabel(2, "Curb Mobility, LLC");
      hns->GetAxis(i)->SetBinLabel(3, "");
      hns->GetAxis(i)->SetBinLabel(4, "");
      hns->GetAxis(i)->SetBinLabel(5, "");
      hns->GetAxis(i)->SetBinLabel(6, "Myle Technologies Inc");
      hns->GetAxis(i)->SetBinLabel(7, "Helix");
    }
    else if (column_names[i] == "RatecodeID") {
      isCategorical[i] = true;
      hns->GetAxis(i)->SetBinLabel(1, "Standard rate");
      hns->GetAxis(i)->SetBinLabel(2, "JFK");
      hns->GetAxis(i)->SetBinLabel(3, "Newark");
      hns->GetAxis(i)->SetBinLabel(4, "Nassau or Westchester");
      hns->GetAxis(i)->SetBinLabel(5, "Negotiated fare");
      hns->GetAxis(i)->SetBinLabel(5, "Group ride");
      // hns->GetAxis(i)->SetBinLabel(99, "Null/unknown");
    }
    else if (column_names[i] == "store_and_fwd_flag") {
      isCategorical[i] = true;
      hns->GetAxis(i)->SetBinLabel(1, "Y");
      hns->GetAxis(i)->SetBinLabel(2, "N");
    }
    else if (column_names[i] == "payment_type") {
      isCategorical[i] = true;
      hns->GetAxis(i)->SetBinLabel(1, "Flex Fare trip");
      hns->GetAxis(i)->SetBinLabel(2, "Credit card");
      hns->GetAxis(i)->SetBinLabel(3, "Cash");
      hns->GetAxis(i)->SetBinLabel(4, "No charge");
      hns->GetAxis(i)->SetBinLabel(5, "Dispute");
      hns->GetAxis(i)->SetBinLabel(5, "Unknown");
      hns->GetAxis(i)->SetBinLabel(7, "Voided trip");
    }
    // else if (column_names[i] == "passenger_count") {
    //   isCategorical[i] = true;
    //   for (int bin = 1; bin <= nbins[i]; ++bin) {
    //     hns->GetAxis(i)->SetBinLabel(bin, TString::Format("%d", bin - 1).Data());
    //   }
    // }
  }

  // Print info from hns
  for (int i = 0; i < hns->GetNdimensions(); ++i) {
    if (isCategorical[i]) {
      NLogTrace("Axis %d: Name='%s', Title='%s', Bins=%d (Categorical)", i, hns->GetAxis(i)->GetName(),
                             hns->GetAxis(i)->GetTitle(), hns->GetAxis(i)->GetNbins());
    }
    else {
      NLogTrace("Axis %d: Name='%s', Title='%s', Bins=%d, Xmin=%.2f, Xmax=%.2f", i,
                             hns->GetAxis(i)->GetName(), hns->GetAxis(i)->GetTitle(), hns->GetAxis(i)->GetNbins(),
                             hns->GetAxis(i)->GetXmin(), hns->GetAxis(i)->GetXmax());
    }
  }

  return hns;
}
