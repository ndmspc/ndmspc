#include <THnSparse.h>
#include <TFile.h>
#include <TH1D.h>
#include <TCanvas.h>

// Default: root://eos.ndmspc.io//eos/ndmspc/scratch/alice/rsn/hDataMap.root
Int_t CreateDataMap(TString output = "hDataMap.root")
{

  std::vector<std::string> data;
  data.push_back("x");
  data.push_back("LHC16f_lowB");
  data.push_back("LHC17g");
  data.push_back("LHC22o_pass4_minBias_small");
  data.push_back("LHC22o_pass4_minBias_medium");
  data.push_back("LHC22o_pass6_minBias_small");

  std::vector<std::string> mc;
  mc.push_back("x");
  mc.push_back("LHC21g3a");
  mc.push_back("LHC21g5a");
  mc.push_back("LHC22b1a");
  mc.push_back("LHC23k2f");
  mc.push_back("LHC24b1b");

  std::vector<TAxis *> axes;
  TAxis *              axis = nullptr;
  axis = new TAxis(10, 0., 10.);
  axis->SetNameTitle("rev", "Revision number");
  axes.push_back(axis);
  axis = new TAxis(8, 65., 73.);
  axis->SetNameTitle("ncls", "TPC N clusters");
  axes.push_back(axis);

  const int nbins = 2 + axes.size();

  Int_t    bins[nbins];
  Double_t min[nbins];
  Double_t max[nbins];
  bins[0] = data.size();
  min[0]  = 0;
  max[0]  = data.size();

  bins[1] = mc.size();
  min[1]  = 0;
  max[1]  = mc.size();

  int i = 2;

  for (auto & a : axes) {
    bins[i] = a->GetNbins();
    min[i]  = a->GetXmin();
    max[i]  = a->GetXmax();
    i++;
  }

  THnSparseD * hDataMap = new THnSparseD("hDataMap", "Data Map", nbins, bins, min, max);

  i = 1;
  hDataMap->GetAxis(0)->SetNameTitle("data", "Data");
  for (auto & d : data) {
    hDataMap->GetAxis(0)->SetBinLabel(i, d.c_str());
    i++;
  }

  i = 1;
  hDataMap->GetAxis(1)->SetNameTitle("mc", "Monte Carlo");
  for (auto & m : mc) {
    hDataMap->GetAxis(1)->SetBinLabel(i, m.c_str());
    i++;
  }

  i = 2;

  for (auto & a : axes) {
    hDataMap->GetAxis(i)->SetNameTitle(a->GetName(), a->GetTitle());
    i++;
  }

  TH1 * h = hDataMap->Projection(1);
  for (int i = 1; i <= hDataMap->GetAxis(1)->GetNbins(); i++) {
    h->GetXaxis()->SetBinLabel(i, hDataMap->GetAxis(1)->GetBinLabel(i));
  }

  TCanvas * c = new TCanvas();
  c->cd(1);
  h->Draw();

  TFile * fOut = TFile::Open(output.Data(), "RECREATE");
  hDataMap->Print("all");
  hDataMap->Write();
  fOut->Close();

  return 0;
}