#include <TRandom3.h>
#include <TFitResult.h>
#include <TMath.h>
#include <TH1D.h>
#include <TCanvas.h>
#include <TFile.h>
#include <TSystem.h>

// To import
//    auto ngnt = Ndmspc::NGnTree::ImportSimple("/tmp/test","Gaus.root",{"entries","mean","sigma"},"ngnt_imported.root","hParameters")

void Gaus(int nEntries = 10000, double mean = 0.0, double sigma = 1.0, std::string outDir = "", std::string outFileName = "Gaus.root")
{

  TCanvas *c1 = new TCanvas("c1", "Gaussian Fit", 800, 600);
  TH1D * h = new TH1D("h", "Gaussian", 200, -10, 10);
  // Fill histogram with Gaussian random numbers 10,000 times
  for (int i = 0; i < nEntries; i++) {
    double x = gRandom->Gaus(mean, sigma);
    h->Fill(x);
  }

  // Retrieve fit results and store them in the parameters of the point
  TFitResultPtr fitResult = h->Fit("gaus", "QS");
  Printf("Fit results: mean = %f ± %f, sigma = %f ± %f", fitResult->Parameter(1), fitResult->Error(1),
         fitResult->Parameter(2), fitResult->Error(2));

  TH1D *hParameters = new TH1D("hParameters", "Fit Parameters", 2, 0., 2.);
  if (!hParameters) {
    Printf("Could not create parameters histogram !!!");
    return;
  }
  hParameters->GetXaxis()->SetBinLabel(1, "Mean");
  hParameters->GetXaxis()->SetBinLabel(2, "Sigma");
  hParameters->SetBinContent(1, fitResult->Parameter(1));
  hParameters->SetBinError(1, fitResult->Error(1));
  hParameters->SetBinContent(2, fitResult->Parameter(2));
  hParameters->SetBinError(2, fitResult->Error(2));

  std::string outFileNameStr = outFileName;
  if (!outDir.empty()) {
    outFileNameStr = TString::Format("%s/%d/%0.2f/%0.2f/%s", outDir.c_str(), nEntries, mean,sigma, outFileName.c_str()).Data();
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
