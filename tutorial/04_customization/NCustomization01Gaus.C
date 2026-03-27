#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>
#include <TF1.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>

void NCustomization01Gaus(int nEntries = 1e5, std::string outFile = "NCustomization01Gaus.root")
{
  ///
  /// One can set export ROOT_MAX_THREADS=4 to run with 4 threads before starting this macro in bash
  ///   e.g. export ROOT_MAX_THREADS=4
  ///

  json cfg;
  cfg["nEntries"] = nEntries;

  // Create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from -2.5 to 2.5 with 5 bins
  TAxis * a1 = new TAxis(5, -2.5, 2.5);
  // Set name and title
  a1->SetNameTitle("mean", "Mean");
  // add axis to the list of axes
  axes->Add(a1);

  TAxis * a2 = new TAxis(5, 0.5, 5.5);
  a2->SetNameTitle("sigma", "Sigma");
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
    // Create Gaussian histogram for each point
    std::string title = "Gauss " + point->GetString();
    TH1D *      h     = new TH1D("hGaus", title.c_str(), 200, -10, 10);

    // Retrieve mean and sigma from the bin centers of current point
    double mean  = point->GetBinCenter("mean");
    double sigma = point->GetBinCenter("sigma");

    // Retrieve configuration
    json cfg = point->GetCfg();

    // Retrieve number of entries
    int n = cfg["nEntries"].get<int>();

    // each thread gets its own RNG (thread-safe)
    thread_local TRandom3 rnd(0); 
    for (int i = 0; i < n; i++) {
      double x = rnd.Gaus(mean, sigma);
      h->Fill(x);
    }

    // Warning: Make sure that you add this canvas to the output list of the point.
    //          If not you have to delete it manually to avoid memory leaks.
    TCanvas * c = Ndmspc::NUtils::CreateCanvas("cGaus", title);

    // Create Gaussian fit function for the histogram
    TF1 *         gausFunc  = new TF1("gausFunc", "gaus", -10, 10);
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

    // Fill output list for the current point with the canvas containing the histogram and fit results
    outputPoint->Add(h);
    outputPoint->Add(c);
  };

  // Define the begin function which is executed before processing all points
  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    NLogInfo("Starting processing ...");
    TH1::AddDirectory(kFALSE);
  };

  // Define the end function which is executed after processing all points
  Ndmspc::NGnEndFuncPtr endFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    NLogInfo("Finished processing ...");
  };

  // execute the processing function
  ngnt->Process(processFunc, cfg, "", beginFunc, endFunc);

  // Clean up
  delete ngnt;
}
