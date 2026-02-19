#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>

void NNested01Gaus(int nEntries = 1e5, std::string outFile = "NNested01Gaus.root")
{
  ///
  /// One can set export ROOT_MAX_THREADS=4 to run with 4 threads before starting this macro in bash
  ///   e.g. export ROOT_MAX_THREADS=4
  ///

  json cfg;

  // Create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from -2.5 to 2.5 with 5 bins
  TAxis * a1 = Ndmspc::NUtils::CreateAxisFromLabels("mean", "Mean", {"-2.5", "-1.25", "0.0", "1.25", "2.5"});
  axes->Add(a1);

  TAxis * a2 = Ndmspc::NUtils::CreateAxisFromLabels("sigma", "Sigma", {"0.5", "1.0", "1.5", "2.0", "2.5", "3.0"});
  axes->Add(a2);

  TAxis * a3 = Ndmspc::NUtils::CreateAxisFromLabels("entries", "Entries", {"100", "1000", "10000", "100000", "1000000"});
  axes->Add(a3);

  // Create an NGnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  // Define the binning for the axes
  std::map<std::string, std::vector<std::vector<int>>> b;
  // Set binning for axis1 (rebin to 1 bin)
  b["mean"]    = {{1}};
  b["sigma"]   = {{1}};
  b["entries"] = {{1}};
  // Create the binning definition with name "default" in the NGnTree
  ngnt->GetBinning()->AddBinningDefinition("default", b);

  ngnt->InitParameters({"meanFit", "sigmaFit"});

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int /*threadId*/) {
    // print the title of the binning point
    NLogInfo("title : %s", point->GetString().c_str());

    // Create Gaussian histogram for each point
    // TRandom3 rnd(0);
    TH1D * h = new TH1D("h", "Gaussian", 200, -10, 10);

    // Retrieve mean and sigma from the bin centers of current point
    double mean  = std::stof(point->GetBinLabel("mean"));
    double sigma = std::stof(point->GetBinLabel("sigma"));

    // Retrieve configuration
    json cfg = point->GetCfg();

    // Retrieve number of entries
    int nEntries = std::stoi(point->GetBinLabel("entries"));

    for (int i = 0; i < nEntries; i++) {
      double x = gRandom->Gaus(mean, sigma);
      h->Fill(x);
    }

    // Retrieve fit results and store them in the parameters of the point
    // Note: "O" option is    // Note: "O" option is used to suppress drawing, but function is stored in the histogram
    TFitResultPtr         fitResult   = h->Fit("gaus", "QSON");
    Ndmspc::NParameters * pointParams = point->GetParameters();
    if (pointParams) {
      pointParams->SetParameter("meanFit", fitResult->Parameter(1), fitResult->Error(1));
      pointParams->SetParameter("sigmaFit", fitResult->Parameter(2), fitResult->Error(2));
    }

    // Fill output list for the current point
    outputPoint->Add(h);
  };

  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    NLogInfo("Starting processing ...");
    TH1::AddDirectory(kFALSE); // Prevent histograms from being associated with the current directory
  };

  Ndmspc::NGnEndFuncPtr endFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    NLogInfo("Finished processing ...");
  };

  // execute the processing function
  ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);

  // close the NGnTree object
  ngnt->Close(true);

  // Clean up
  delete ngnt;
}
