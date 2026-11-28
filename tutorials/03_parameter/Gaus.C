#include <TRandom3.h>
#include <TFitResult.h>
#include <TMath.h>
#include <TH1D.h>
#include <TCanvas.h>
#include <TFile.h>
#include <TROOT.h>
#include <TSystem.h>

#include <vector>
#include <tuple>
#include <ndmspc/core/NGnTree.h>

// Generate several combinations with Gaus(...) and merge them with:
//    auto ngnt = Ndmspc::NGnTree::ImportSimple("/tmp/test","Gaus.root",{"entries","mean","sigma"},{},{},
//                                              "ngnt_imported.root","hParameters")
// or
//    auto ngnt = Ndmspc::NGnTree::Import("import.json") // import from JSON file

void Gaus(std::string outDir = "", int nEntries = 10000, double mean = 0.0, double sigma = 1.0,
          std::string outFileName = "Gaus.root")
{

  gROOT->SetBatch(kTRUE); // Run in batch mode to avoid opening GUI windows

  std::string title = TString::Format("Gaussian Fit: nEntries=%d, mean=%.2f, sigma=%.2f", nEntries, mean, sigma).Data();
  TCanvas *   c1    = new TCanvas("cGaus", title.c_str(), 800, 600);
  TH1D *      h     = new TH1D("hGaus", title.c_str(), 200, -10, 10);
  // Fill histogram with Gaussian random numbers 10,000 times
  for (int i = 0; i < nEntries; i++) {
    double x = gRandom->Gaus(mean, sigma);
    h->Fill(x);
  }

  // Retrieve fit results and store them in the parameters of the point
  TFitResultPtr fitResult = h->Fit("gaus", "QS");
  Printf("Fit results: mean = %f ± %f, sigma = %f ± %f", fitResult->Parameter(1), fitResult->Error(1),
         fitResult->Parameter(2), fitResult->Error(2));

  TH1D * hParameters = new TH1D("hParameters", "Fit Parameters", 2, 0., 2.);
  if (!hParameters) {
    Printf("Could not create parameters histogram !!!");
    return;
  }
  hParameters->GetXaxis()->SetBinLabel(1, "meanFit");
  hParameters->GetXaxis()->SetBinLabel(2, "sigmaFit");
  hParameters->SetBinContent(1, fitResult->Parameter(1));
  hParameters->SetBinError(1, fitResult->Error(1));
  hParameters->SetBinContent(2, fitResult->Parameter(2));
  hParameters->SetBinError(2, fitResult->Error(2));

  std::string outFileNameStr = outFileName;
  if (!outDir.empty()) {
    outFileNameStr =
        TString::Format("%s/%d/%0.2f/%0.2f/%s", outDir.c_str(), nEntries, mean, sigma, outFileName.c_str()).Data();
    gSystem->mkdir(TString::Format("%s/%d/%0.2f/%0.2f", outDir.c_str(), nEntries, mean, sigma).Data(), true);
  }

  TFile * fOut = TFile::Open(outFileNameStr.c_str(), "RECREATE");
  if (!fOut) {
    Printf("Could not create output file %s", outFileNameStr.c_str());
    return;
  }

  c1->Write();
  h->Write();
  hParameters->Write();

  fOut->Close();

  Printf("Results saved to '%s' ...", outFileNameStr.c_str());
}

// Generate 5 Gaus combinations and merge them into a single NGnTree
void GausImport(std::string outDir = "/tmp/test", std::string outFileName = "ngnt_imported.root")
{
  // 5 combinations of (nEntries, mean, sigma) - a mean scan forming a complete grid
  // (ImportSimple builds the axes from the directory names, so all combinations must be present)
  const std::vector<std::tuple<int, double, double>> combinations = {
      {100, -2.0, 1.0}, {1000, -1.0, 1.0}, {10000, 0.0, 1.0}, {1000, 1.0, 1.0}, {10000, 2.0, 1.0},
  };

  for (const auto & [nEntries, mean, sigma] : combinations) {
    Gaus(outDir, nEntries, mean, sigma);
  }

  // Merge all generated files into a single NGnTree
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::ImportSimple(outDir, "Gaus.root", {"entries", "mean", "sigma"}, {}, {},
                                                         outFileName, "hParameters");
  if (!ngnt) {
    Printf("Could not import 'Gaus.root' from '%s' !!!", outDir.c_str());
    return;
  }
  Printf("Merged %zu combinations from '%s' into '%s' ...", combinations.size(), outDir.c_str(), outFileName.c_str());
  delete ngnt;
}
