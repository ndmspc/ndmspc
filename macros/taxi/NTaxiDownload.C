#include <string>
#include <TSystem.h>
#include <TString.h>
#include <TObjString.h>
#include <TInterpreter.h>
#include <TROOT.h>
#include <TFile.h>
#include <THnSparse.h>
#include <TCanvas.h>
#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NUtils.h>

void NTaxiDownload(std::string outFile = "NTaxiDownload.root", std::string outputPrefix = "$HOME/.ngnt_taxi",
                   std::string inputPrefix = "root://eos.ndmspc.io//eos/ndmspc/scratch/taxi/src",
                   std::string inputSuffix = ".parquet")
{

  TObjArray *              axes       = new TObjArray();
  std::vector<std::string> yearLabels = {"2009", "2010", "2011", "2012", "2013", "2014", "2015", "2016", "2017",
                                         "2018", "2019", "2020", "2021", "2022", "2023", "2024", "2025"};
  yearLabels                          = {"2023", "2024"};
  TAxis * aYear                       = Ndmspc::NUtils::CreateAxisFromLabels("year", "Year", yearLabels);
  axes->Add(aYear);

  std::vector<std::string> monthLabels = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  // monthLabels                          = {"JAN", "FEB"};
  TAxis * aMonth =
      Ndmspc::NUtils::CreateAxisFromLabels("month", "Month",
                                           monthLabels); // Note: monthsMin is 1-based index, so we use monthsMin - 1
  axes->Add(aMonth);

  // std::vector<std::string> taxiTypeLabels = {"yellow", "green", "fhv", "fhvhv"};
  // // std::vector<std::string> taxiTypeLabels = {"yellow", "green", "fhv"};
  std::vector<std::string> taxiTypeLabels = {"yellow", "green"};
  // std::vector<std::string> taxiTypeLabels = {"yellow"};
  TAxis * aTaxiType = Ndmspc::NUtils::CreateAxisFromLabels("taxi_type", "Taxi Type", taxiTypeLabels);
  axes->Add(aTaxiType);

  // Create an NHnSparseObject from the THnSparse
  Ndmspc::NGnTree * ngnt_taxi = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  b["year"]      = {{1}};
  b["month"]     = {{1}};
  b["taxi_type"] = {{1}};

  ngnt_taxi->GetBinning()->AddBinningDefinition("default", b);

  // Remove trailing slash if present
  if (!inputPrefix.empty() && inputPrefix.back() == '/') {
    inputPrefix.pop_back();
  }
  if (!outputPrefix.empty() && outputPrefix.back() == '/') {
    outputPrefix.pop_back();
  }

  json cfg = json::object();
  // cfg["opt"]               = "A";
  cfg["inputPrefix"]  = inputPrefix;
  cfg["inputSuffix"]  = inputSuffix;
  cfg["outputPrefix"] = outputPrefix;

  // cfg["opt"]               = "A";
  // cfg["inputPrefix"] = "https://d37ci6vzurychx.cloudfront.net/trip-data/";
  // cfg["inputPrefix"]  = "root://eos.ndmspc.io//eos/ndmspc/scratch/taxi/src/";
  // cfg["inputPrefix"] = "/home/mvala/.ngnt_taxi/";
  // cfg["inputSuffix"] = ".parquet";
  // cfg["outputPrefix"] = "/home/mvala/.ngnt_taxi/";
  // cfg["outputPrefix"] = "/home/mvala/.ngnt_taxi_2/";
  // cfg["outputPrefix"] = "root://eos.ndmspc.io//eos/ndmspc/scratch/taxi/tmp/src/";
  // cfg["outputPrefix"] = "root://eos.ndmspc.io//eos/ndmspc/scratch/taxi/tmp/ngnt_taxi/";
  // "yellow_tripdata_2025-01.parquet";
  // gSystem->Exec(TString::Format("mkdir -p %s", cfg["outputPrefix"].get<std::string>().c_str()).Data());

  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // NLogInfo("Thread ID: %d", threadId);

    if (!point) {
      NLogError("NTaxiDownload: Point is nullptr !!!");
      return;
    }
    // point->Print("A");

    json cfg = point->GetCfg();

    std::string fileTypeYearMonth = point->GetLabels()[2] + "_tripdata_" + point->GetLabels()[0] + "-" +
                                    TString::Format("%02d", point->GetStorageCoords()[1]).Data();
    std::string inputFile =
        cfg["inputPrefix"].get<std::string>() + "/" + fileTypeYearMonth + cfg["inputSuffix"].get<std::string>();
    std::string outputFile =
        cfg["outputPrefix"].get<std::string>() + "/" + fileTypeYearMonth + cfg["inputSuffix"].get<std::string>();

    // check if outputFile already exists

    if (Ndmspc::NUtils::AccessPathName(outputFile.c_str()) == true) {
      NLogInfo("NTaxiDownload: File %s already exists. Skipping download ...", outputFile.c_str());
      outputPoint->Add(new TH1S("filename", outputFile.c_str(), 1, 0, 1));
      return;
    }

    if (Ndmspc::NUtils::AccessPathName(inputFile.c_str()) == false) {
      NLogError("NTaxiDownload: Input file %s does not exist !!! Skipping ...", inputFile.c_str());
      return;
    }
    else {
      NLogInfo("NTaxiDownload: Input file %s exists.", inputFile.c_str());
      NLogInfo("NTaxiDownload: Copy %s to %s ...", inputFile.c_str(), outputFile.c_str());
      // TODO: This works for TFile::Cp
      inputFile += "?filetype=raw";
      // TFile::Cp(inputFile.c_str(), outputFile.c_str(), false);
      Ndmspc::NUtils::Cp(inputFile.c_str(), outputFile.c_str(), false);
      // gSystem->Exec(TString::Format("curl -s -o %s %s", outputFile.c_str(), inputFile.c_str()).Data());
    }

    if (Ndmspc::NUtils::AccessPathName(outputFile.c_str()) == false) {
      NLogError("NTaxiDownload: Failed to download file %s", outputFile.c_str());
      return;
    }

    outputPoint->Add(new TH1S("filename", outputFile.c_str(), 1, 0, 1));

    // gSystem->Exec("ls");
  };

  bool rc = false;
  rc      = ngnt_taxi->Process(processFunc, cfg);
  // ngnt_taxi->Print();

  if (rc) {
    NLogInfo("NTaxiDownload: Processing completed successfully.");
    ngnt_taxi->Close(true);
  }
  else {
    NLogError("NTaxiDownload: Processing failed.");
    ngnt_taxi->Close(false);
  }

  // Clean up
  delete ngnt_taxi;
}
