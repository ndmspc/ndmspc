#include <string>
#include <TAxis.h>
#include <TObjArray.h>
#include <THnSparse.h>
#include <TFile.h>
void Mapping(std::string filename = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/dev/alice/PWGLF-376/hMap.root")
{
  TObjArray *              axes = new TObjArray();
  std::vector<std::string> data;
  data.push_back("x");
  data.push_back("LHC16f_lowB");
  data.push_back("LHC17g");
  data.push_back("LHC22o_pass4_minBias_small");
  data.push_back("LHC22o_pass4_minBias_medium");
  data.push_back("LHC22o_pass6_minBias_small");
  TAxis * dataAxis = new TAxis(data.size(), 0., data.size());
  axes->Add(dataAxis);

  std::vector<std::string> mc;
  mc.push_back("x");
  mc.push_back("LHC21g3a");
  mc.push_back("LHC21g5a");
  mc.push_back("LHC22b1a");
  mc.push_back("LHC23k2f");
  mc.push_back("LHC24b1b");
  TAxis * mcAxis = new TAxis(mc.size(), 0., mc.size());
  axes->Add(mcAxis);

  // Add revision axis
  TAxis * revisionAxis = new TAxis(100, 0., 100.);
  axes->Add(revisionAxis);

  // Add Least number of clusters axis
  TAxis * clustersAxis = new TAxis(100, 0., 100.);
  axes->Add(clustersAxis);

  const int  nDims = axes->GetEntries();
  Int_t *    bins  = new Int_t[nDims];
  Double_t * mins  = new Double_t[nDims];
  Double_t * maxs  = new Double_t[nDims];

  for (int i = 0; i < nDims; i++) {
    TAxis * a = (TAxis *)axes->At(i);
    if (a == nullptr) {
      Printf("Error: axis %d is null !!!", i);
      return;
    }
    bins[i] = a->GetNbins();
    mins[i] = a->GetXmin();
    maxs[i] = a->GetXmax();
  }

  THnSparse * hMap = new THnSparseI("hMap", "N-dimensional Mapping Histogram", nDims, bins, mins, maxs);

  dataAxis = hMap->GetAxis(0);
  dataAxis->SetNameTitle("data", "Data");
  for (int i = 0; i < data.size(); ++i) {
    dataAxis->SetBinLabel(i + 1, data[i].c_str());
  }
  mcAxis = hMap->GetAxis(1);
  mcAxis->SetNameTitle("mc", "Monte Carlo");
  for (int i = 0; i < mc.size(); ++i) {
    mcAxis->SetBinLabel(i + 1, mc[i].c_str());
  }

  hMap->GetAxis(2)->SetNameTitle("revision", "Revision");
  hMap->GetAxis(3)->SetNameTitle("clusters", "Number of Clusters");

  TFile * file = TFile::Open(filename.c_str(), "RECREATE");
  if (file == nullptr) {
    Printf("Error: file '%s' was not open for read/write !!!", filename.c_str());
    return;
  }
  hMap->Write();
  file->Close();

  delete[] bins;
  delete[] mins;
  delete[] maxs;
}
