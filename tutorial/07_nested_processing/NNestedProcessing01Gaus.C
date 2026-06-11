#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>
#include <TF1.h>

void NGausMeanSigma(std::string outFile = "NGausMeanSigma.root", int entries = 10000, bool onlyOddPoints = false)
{
  json cfg;
  cfg["onlyOddPoints"] = onlyOddPoints;
  cfg["entries"]       = entries;

  // Create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from -2.5 to 2.5 with 5 bins
  TAxis * a1 = Ndmspc::NUtils::CreateAxisFromLabels("mean", "Mean", {"-2.5", "-1.25", "0.0", "1.25", "2.5"});
  axes->Add(a1);

  TAxis * a2 = Ndmspc::NUtils::CreateAxisFromLabels("sigma", "Sigma", {"0.5", "1.0", "1.5", "2.0", "2.5", "3.0"});
  axes->Add(a2);

  // Create an NGnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  // Set binning for axis1 (rebin to 1 bin)
  b["mean"]  = {{1}};
  b["sigma"] = {{1}};
  // Create the binning definition with name "default" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  ngnt->InitParameters({"meanFit", "sigmaFit"});

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int threadId) {
    // Retrieve configuration
    json cfg = point->GetCfg();

    if (cfg["onlyOddPoints"].get<bool>() && (point->GetEntryNumber() % 2 == 0)) {
      NLogInfo("[%d] Skipping point: %s", threadId, point->GetString().c_str());
      return;
    }

    // Create Gaussian histogram for each point
    std::string title = "Gauss " + point->GetString();
    TH1D *      h     = new TH1D("hGaus", title.c_str(), 200, -10, 10);

    // Retrieve mean and sigma from the bin centers of current point
    double mean  = std::stof(point->GetBinLabel("mean"));
    double sigma = std::stof(point->GetBinLabel("sigma"));

    // Retrieve number of entries
    int nEntries = cfg["entries"].get<int>();

    // each thread gets its own RNG (thread-safe)
    thread_local TRandom3 rnd(0);
    for (int i = 0; i < nEntries; i++) {
      double x = rnd.Gaus(mean, sigma);
      h->Fill(x);
    }

    // Warning: Make sure that you add this canvas to the output list of the point.
    //          If not you have to delete it manually to avoid memory leaks.
    TCanvas * c = Ndmspc::NUtils::CreateCanvas("cGaus", title);

    // Create Gaussian fit function for the histogram
    TF1 * gausFunc = new TF1("gausFunc", "gaus", -10, 10);
    // gausFunc->AddToGlobalList(false); // prevent registration in ROOT's global list (thread-safe)

    // Retrieve fit results and store them in the parameters of the point
    TFitResultPtr fitResult = h->Fit(gausFunc, "QS");
    NLogInfo("Fit results: [%d] %s mean = %.3f ± %.3f, sigma = %.3f ± %.3f", threadId, point->GetString().c_str(),
             fitResult->Parameter(1), fitResult->Error(1), fitResult->Parameter(2), fitResult->Error(2));

    // Store fit results in the parameters of the point
    Ndmspc::NParameters * pointParams = point->GetParameters();
    if (pointParams) {
      pointParams->SetParameter("meanFit", fitResult->Parameter(1), fitResult->Error(1));
      pointParams->SetParameter("sigmaFit", fitResult->Parameter(2), fitResult->Error(2));
    }
    outputPoint->Add(h);
    outputPoint->Add(c);
  };

  // Define the begin function which is executed before processing all points
  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    // NLogInfo("Starting processing ...");
    // TH1::AddDirectory(kFALSE);
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

void NNestedProcessing01Gaus(std::string outputDir = "/tmp/NNestedProcessing",
                             std::string outFile = "NNestedProcessing01Gaus.root", bool onlyOddPoints = false)
{
  json cfg;
  cfg["outputDir"]     = outputDir; // %d is replaced by the thread id in the output file name
  cfg["outFile"]       = outFile;
  cfg["onlyOddPoints"] = onlyOddPoints;

  // Create axes
  TObjArray * axes = new TObjArray();
  TAxis *     a3 =
      Ndmspc::NUtils::CreateAxisFromLabels("entries", "Entries", {"100", "1000", "10000", "100000", "1000000"});
  axes->Add(a3);

  // Create an NGnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  b["entries"] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    // Retrieve configuration
    json        cfg           = point->GetCfg();
    bool        onlyOddPoints = cfg["onlyOddPoints"].get<bool>();
    int         entries       = std::stoi(point->GetBinLabel("entries"));
    std::string outputDir     = cfg["outputDir"].get<std::string>();
    std::string outFileName   = cfg["outFile"].get<std::string>();
    std::string outFile       = Form("%s/%d/%s", outputDir.c_str(), entries, outFileName.c_str());

    // gSystem->Setenv("NDMSPC_MAX_PROCESSES", "5"); // set temporary directory for NGnTree processing (e.g. for storing
    // intermediate files during import) gSystem->Setenv("NDMSPC_EXECUTION_MODE", "ipc"); // set temporary directory for
    // NGnTree processing (e.g. for storing intermediate files during import)

    NGausMeanSigma(outFile, entries, onlyOddPoints);

    outputPoint->Add(new TH1S("outputFile", outFile.c_str(),1,0,1));
  };

  // Define the begin function which is executed before processing all points
  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    // NLogInfo("Starting processing ...");
    // TH1::AddDirectory(kFALSE);
  };

  // Define the end function which is executed after processing all points
  Ndmspc::NGnEndFuncPtr endFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    // NLogInfo("Finished processing ...");
  };
  // execute the processing function
  ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);
  delete ngnt;

  std::string outFileNameImported = outFile;
  // replace .root with _imported.root in outFileNameImported
  size_t pos = outFileNameImported.rfind(".root");
  if (pos != std::string::npos) {
    outFileNameImported.replace(pos, 5, "_imported.root");
  }
  ngnt = Ndmspc::NGnTree::ImportNgnt(outputDir, outFile, {"entries"}, {},{}, outFileNameImported);
  ngnt->Print();
  delete ngnt;
}
