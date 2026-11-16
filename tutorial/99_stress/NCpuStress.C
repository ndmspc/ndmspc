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

/**
 * The parameter names a tree is built with.
 *
 * `NParameters` keeps them as one `TH1D` with a bin per name, and `Reshape` reads that histogram **bin by
 * bin**, resolving each bin's label to a name (see `NGnNavigator.cxx:830-836`). So a wide `_params` branch
 * is what makes reading a point cost something — "fake for stress", which is what this tutorial is for.
 *
 * Objects do **not** count: `Reshape` disables the `_outputPoint` branch outright
 * (`NGnNavigator.cxx:472`), so extra histograms per point are never read by it.
 */
std::vector<std::string> ParameterNames(int nParamCopies)
{
  std::vector<std::string> names{"meanFit", "sigmaFit"};
  for (int i = 0; i < std::max(0, nParamCopies); i++) names.push_back("fake" + std::to_string(i));
  return names;
}

void NCpuStress(std::string outFile = "NCpuStress.root", Long64_t nEntries = 1e7, int rebinMean = 1,
                int rebinSigma = 1, int nBinsMean = 10, int nBinsSigma = 10, int nParamCopies = 0)
{
  gErrorIgnoreLevel = kError;

  json cfg;
  cfg["nEntries"]     = nEntries;
  cfg["nParamCopies"] = nParamCopies;

  // Create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from -2.5 to 2.5 with 5 bins
  TAxis * a1 = new TAxis(nBinsMean, -5., 5.);
  a1->SetNameTitle("mean", "Mean");
  axes->Add(a1);

  TAxis * a2 = new TAxis(nBinsSigma, 0., 10.);
  a2->SetNameTitle("sigma", "Sigma");
  axes->Add(a2);

  // Create an NGnTree from the list of axes
  Ndmspc::NGnTree * ngnt = new Ndmspc::NGnTree(axes, outFile);

  std::map<std::string, std::vector<std::vector<int>>> b0;
  b0["mean"]  = {{rebinMean}};
  b0["sigma"] = {{rebinSigma}};
  ngnt->GetBinning()->AddBinningDefinition("b0", b0);

  ngnt->InitParameters(ParameterNames(nParamCopies));

  // Define the processing function
  Ndmspc::NGnProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                             int threadId) {
    // Retrieve configuration
    json cfg = point->GetCfg();

    // Create Gaussian histogram for each point
    std::string title = "Gauss " + point->GetString();
    TH1D *      h     = new TH1D("hGaus", title.c_str(), 200, -10, 10);

    // Retrieve mean and sigma from the bin centers of current point
    double mean  = point->GetBinCenter("mean");
    double sigma = point->GetBinCenter("sigma");

    // Retrieve number of entries
    Long64_t nEntries = 1e7;

    if (cfg.contains("nEntries")) {
      nEntries = cfg["nEntries"].get<Long64_t>();
    }

    // each thread gets its own RNG (thread-safe)
    thread_local TRandom3 rnd(0);
    for (Long64_t i = 0; i < nEntries; i++) {
      double x = rnd.Gaus(mean, sigma);
      h->Fill(x);
    }

    // // Warning: Make sure that you add this canvas to the output list of the point.
    // //          If not you have to delete it manually to avoid memory leaks.
    // TCanvas * c = Ndmspc::NUtils::CreateCanvas("cGaus", title);

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

      // Ballast parameters, so the `_params` branch is as wide as the caller asked for. A reshape reads
      // all of them for every point, which is the cost this tutorial exists to make measurable.
      const int nCopies = cfg.contains("nParamCopies") ? cfg["nParamCopies"].get<int>() : 0;
      for (int i = 0; i < nCopies; i++) {
        pointParams->SetParameter(("fake" + std::to_string(i)).c_str(), fitResult->Parameter(1) + i,
                                  fitResult->Error(1));
      }
    }

    outputPoint->Add(h);
    // outputPoint->Add(c);
  };

  // Define the begin function which is executed before processing all points
  Ndmspc::NGnBeginFuncPtr beginFunc = [](Ndmspc::NBinningPoint * /*point*/, int /*threadId*/) {
    // NLogInfo("Starting processing ...");
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
