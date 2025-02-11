#include <TFile.h>
#include <TH2.h>
#include "HnSparse.h"
#include "HnSparseStress.h"
#include "ndmspc.h"

int main(int argc, char ** argv)
{
  Printf("%s v%s-%s", NDMSPC_NAME, NDMSPC_VERSION, NDMSPC_VERSION_RELEASE);

  Long64_t    size        = 1e5;
  bool        bytes       = false;
  std::string filename    = "/tmp/HnSparse.root";
  bool        projections = false;

  int      nDim      = 10;
  int      nBins     = 1000;
  double   min       = -(double)nBins / 2;
  double   max       = (double)nBins / 2;
  Long64_t chunkSize = 1024 * 1024;

  Int_t    bins[nDim];
  Double_t mins[nDim];
  Double_t maxs[nDim];
  for (Int_t i = 0; i < nDim; i++) {
    bins[i] = nBins;
    mins[i] = min;
    maxs[i] = max;
  }

  Ndmspc::Ndh::HnSparseD * h = new Ndmspc::Ndh::HnSparseD("hNStress", "hNStress", nDim, bins, mins, maxs, chunkSize);

  Ndmspc::Ndh::HnSparseStress stress;

  TStopwatch timeStress;
  timeStress.Start();
  stress.Stress(h, size, bytes);
  timeStress.Stop();
  timeStress.Print("m");
  h->Print();
  Long64_t nBinsSizeBytes = sizeof(Double_t) * h->GetNbins();
  Printf("size : %03.2f MB (%lld B)", (double)nBinsSizeBytes / (1024 * 1024), nBinsSizeBytes);

  TStopwatch timeWrite;
  timeWrite.Start();
  TFile * f = TFile::Open(filename.data(), "RECREATE");
  f->SetCompressionSettings(ROOT::RCompressionSetting::EDefaults::kUseAnalysis);
  Printf("Writing histogram ...");
  h->Write();
  if (projections) {
    Printf("Creating and writing projections ...");
    for (Int_t i = 0; i < nDim; i++) {
      ((TH1 *)h->Projection(i))->Write();
      for (Int_t j = 0; j < nDim; j++) {
        if (i == j) continue;
        ((TH2 *)h->Projection(j, i))->Write();
      }
    }
  }
  f->Close();
  timeWrite.Stop();
  timeWrite.Print("m");
  Printf("Output was saved in '%s' size : %.2f MB (%lld B)", filename.data(),
         (double)f->GetBytesWritten() / (1024 * 1024), f->GetBytesWritten());

  return 0;
}
