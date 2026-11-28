/// toolGaus.C — the skeleton for the second exercise.
///
/// Gaus(...) is the function to run: it fills a histogram with Gaussian random numbers and fits it.
/// The tool around it is empty, so nothing happens yet.
///
///   cd ~/ndmspc-tutorial/20261119-alice
///   ndmspc-server --no-default-tools -m 05_your_own_tool/toolGaus.C
///
/// The worked version is toolGausSolution.C.

#include <map>
#include <string>

#include <TFitResult.h>
#include <TH1D.h>
#include <TObject.h>
#include <TRandom.h>

#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NRouteContext.h>

/// What the tool runs: fill a histogram with `nEntries` Gaussian random numbers of the given
/// `mean` and `sigma`, fit it, and hand the histogram back. The fit is stored on the histogram
/// (`h->GetFunction("gaus")`), so the caller can read the fitted parameters from it.
TH1D *Gaus(int nEntries = 10000, double mean = 0.0, double sigma = 1.0)
{
  TH1D *h = new TH1D("hGaus", "Gaussian", 200, -10, 10);

  for (int i = 0; i < nEntries; i++) {
    h->Fill(gRandom->Gaus(mean, sigma));
  }

  TFitResultPtr fit = h->Fit("gaus", "QS");

  Printf("mean = %f ± %f, sigma = %f ± %f", fit->Parameter(1), fit->Error(1), fit->Parameter(2),
         fit->Error(2));

  return h;
}

/// An empty tool: register the action, attach its handler, and it exists on all three interfaces.
void toolGaus()
{
  auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // TODO: register the action(s) here, then attach the handler(s).
}
