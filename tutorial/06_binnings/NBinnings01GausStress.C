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

void NBinnings01GausStress(std::string outFile = "NBinnings01Gaus.root", bool onlyOddPoints = false)
{
  ///
  /// One can set export ROOT_MAX_THREADS=4 to run with 4 threads before starting this macro in bash
  ///   e.g. export ROOT_MAX_THREADS=4
  ///

  gErrorIgnoreLevel = kError;

  json cfg;
  cfg["onlyOddPoints"] = onlyOddPoints;

  // Create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from -2.5 to 2.5 with 5 bins
  TAxis * a1 = new TAxis(100, -5., 5.);
  a1->SetNameTitle("mean", "Mean");
  axes->Add(a1);

  TAxis * a2 = new TAxis(100, 0., 10.);
  a2->SetNameTitle("sigma", "Sigma");
  axes->Add(a2);

  TAxis * a3 =
      Ndmspc::NUtils::CreateAxisFromLabels("entries", "Entries", {"10", "100", "1000", "10000", "100000", "1000000"});
  // Ndmspc::NUtils::CreateAxisFromLabels("entries", "Entries", {"10"});
  axes->Add(a3);

  // Create an NGnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);


  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b0x;
  // Set binning for axis1 (rebin to 1 bin)
  b0x["mean"]    = {{100}};
  b0x["sigma"]   = {{100}};
  b0x["entries"] = {{1}};
  // Create the binning definition with name "b0" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b0x", b0x);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b0;
  // Set binning for axis1 (rebin to 1 bin)
  b0["mean"]    = {{50}};
  b0["sigma"]   = {{50}};
  b0["entries"] = {{1}};
  // Create the binning definition with name "b0" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b0", b0);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b0_;
  // Set binning for axis1 (rebin to 1 bin)
  b0_["mean"] = {{50, 1}, {25}};
  // b0_["mean"]    = {{50}};
  b0_["sigma"]   = {{50}};
  b0_["entries"] = {{1}};
  // Create the binning definition with name "b0" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b0_", b0_);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b1;
  // Set binning for axis1 (rebin to 1 bin)
  b1["mean"]    = {{5}};
  b1["sigma"]   = {{5}};
  b1["entries"] = {{1}};
  // Create the binning definition with name "b1" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b1", b1);

  std::map<std::string, std::vector<std::vector<int>>> b2;
  b2["mean"]    = {{10}};
  b2["sigma"]   = {{10}};
  b2["entries"] = {{1}};
  // Create the binning definition with name "b2" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b2", b2);

  std::map<std::string, std::vector<std::vector<int>>> b3;
  // b3["mean"]    = {{5,2}, {10}};
  b3["mean"]    = {{10}};
  b3["sigma"]   = {{10}};
  b3["entries"] = {{1}};
  // Create the binning definition with name "b3" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b3", b3);

  std::map<std::string, std::vector<std::vector<int>>> b4;
  // b3["mean"]    = {{5,2}, {10}};
  b4["mean"]    = {{3}};
  b4["sigma"]   = {{3}};
  b4["entries"] = {{1}};
  // Create the binning definition with name "b4" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("b4", b4);
  
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
    double mean  = point->GetBinCenter("mean");
    double sigma = point->GetBinCenter("sigma");

    // Retrieve number of entries
    int nEntries = std::stoi(point->GetBinLabel("entries"));

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
    gausFunc->AddToGlobalList(false); // prevent registration in ROOT's global list (thread-safe)

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
