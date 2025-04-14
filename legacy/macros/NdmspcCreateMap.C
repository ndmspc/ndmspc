#include <TFile.h>
#include <TH1.h>
#include <THnSparse.h>
#include <TString.h>
#include <sstream>

std::vector<std::string> GetListObObjects(const char * str, char token = ',')
{
  std::vector<std::string> result;
  std::stringstream        strs(str);
  std::string              item;
  while (std::getline(strs, item, ',')) {
    if (item.empty()) continue;
    result.push_back(item);
  }
  return result;
}

Int_t NdmspcCreateMap(TString filename = "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/cern.ch/user/a/alihyperloop/"
                                         "outputs/0013/138309/16826/AnalysisResults.root",
                      TString dir = "phianalysis-t-hn-sparse_MC_analysis/unlike", TString extra = "",
                      TString output      = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/demo/phi/hMap.root",
                      TString hMapObjName = "hMap", TString fileOpt = "?remote=1")
{

  TH1::AddDirectory(kFALSE);

  TFile * fIn = TFile::Open(filename.Data());
  if (!fIn) {
    Printf("Error: Could not open input file '%s' !!!", filename.Data());
    return 1;
  }

  THnSparse * hIn = (THnSparse *)fIn->Get(dir.Data());
  if (!hIn) {
    Printf("Error: Could not open object '%s' !!!", dir.Data());
    return 2;
  }
  hIn->SetNameTitle("hMap", filename.Data());
  hIn->Reset();

  std::vector<std::string> extraObjects = GetListObObjects(extra.Data(), ',');

  TFile * fOut = TFile::Open(TString::Format("%s%s", output.Data(), fileOpt.Data()).Data(), "RECREATE");
  if (!fOut) {
    Printf("Error: Could not open output file '%s' !!!", output.Data());
    return 3;
  }

  hIn->Write();
  TObject * obj;
  for (auto & o : extraObjects) {
    Printf("Adding '%s' ...", o.c_str());
    obj = fIn->Get(o.c_str());
    if (obj) obj->Write();
  }

  fIn->Close();
  fOut->Close();

  Printf("Output file: %s", output.Data());

  return 0;
}
