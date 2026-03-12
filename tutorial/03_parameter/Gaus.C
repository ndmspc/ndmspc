#include <TRandom3.h>
#include <TFitResult.h>
#include <TMath.h>
#include <TH1D.h>

void Gaus(int nEntries = 10000, double mean = 0.0, double sigma = 1.0)
{

  TH1D * h = new TH1D("h", "Gaussian", 200, -10, 10);
  // Fill histogram with Gaussian random numbers 10,000 times
  for (int i = 0; i < nEntries; i++) {
    double x = gRandom->Gaus(mean, sigma);
    h->Fill(x);
  }

  // Retrieve fit results and store them in the parameters of the point
  TFitResultPtr fitResult = h->Fit("gaus", "QS");
  Printf("Fit results: mean = %f ± %f, sigma = %f ± %f\n", fitResult->Parameter(1), fitResult->Error(1),
         fitResult->Parameter(2), fitResult->Error(2));
}
