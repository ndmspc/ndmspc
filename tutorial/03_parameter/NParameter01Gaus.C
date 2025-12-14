#include <TAxis.h>
#include <TObjArray.h>
#include <TFitResult.h>
#include <NGnTree.h>
#include <NLogger.h>
#include <NUtils.h>
#include <TRandom3.h>
#include <TMath.h>
#include <TH1D.h>

void NParameter01Gaus(std::string outFile = "NParameter01Gaus.root")
{

  // Create axes
  TObjArray * axes = new TObjArray();

  // Create a linear axis from 0 to 3 with 3 bins
  TAxis * a1 = new TAxis(3, -3.5, 3.5);
  // set name and title
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
  Ndmspc::NHnSparseProcessFuncPtr processFunc = [](Ndmspc::NBinningPoint * point, TList * output, TList * outputPoint,
                                                   int threadId) {
    // print the title of the binning point
    NLogInfo("title : %s", point->GetString().c_str());

    // Create Gaussian histogram for each point
    // TRandom3 rnd(0);
    TH1D * h = new TH1D("h", "Gaussian", 100, -10, 10);

    int mean  = point->GetBinCenter("mean");
    int sigma = point->GetBinCenter("sigma");

    for (int i = 0; i < 1e5; i++) {
      double x = gRandom->Gaus(mean, sigma);
      h->Fill(x);
    }

    TFitResultPtr         fitResult   = h->Fit("gaus", "QS");
    Ndmspc::NParameters * pointParams = point->GetParameters();
    if (pointParams) {
      pointParams->SetParameter("meanFit", fitResult->Parameter(1), fitResult->Error(1));
      pointParams->SetParameter("sigmaFit", fitResult->Parameter(2), fitResult->Error(2));
    }

    outputPoint->Add(h);
  };

  // execute the processing function
  ngnt->Process(processFunc);

  // close the NGnTree object
  ngnt->Close(true);

  // Clean up
  delete ngnt;
}
