#include <TString.h>
#include <TFile.h>
#include <TKey.h>

void RootDirectorySearchExport(TDirectory *current, TString path, TFile *fOut, TString &outDir, TString &contentFileName)
{
  TDirectory *d = nullptr;
  // Printf("Directory %s %s", current->GetName(), path.Data());
  // current->ls();
  TList *keys = current->GetListOfKeys();
  // keys->Print();
  for (Int_t i = 0; i < keys->GetEntries(); i++)
  {

    TKey *key = (TKey *)keys->At(i);
    TString className = key->GetClassName();
    if (className.BeginsWith("TDirectory"))
    {

      d = (TDirectory *)key->ReadObj();
      fOut = nullptr;
      RootDirectorySearchExport(d, TString::Format("%s/%s", path.Data(), d->GetName()), fOut, outDir, contentFileName);
    }
    else
    {
      if (!fOut)
      {
        Printf("Creating file %s ...", TString::Format("%s%s/%s", outDir.Data(), path.Data(), contentFileName.Data()).Data());
        fOut = TFile::Open(TString::Format("%s%s/%s", outDir.Data(), path.Data(), contentFileName.Data()).Data(), "RECREATE");
      }
      TNamed *o = (TNamed *)(key)->ReadObj();
      // o->SetTitle(path.Data());

      if (fOut)
        o->Write();
    }
  }

  if (fOut)
    fOut->Close();
}

Int_t NdmspcFileToDirectory(TString input = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/export/orig/ndmspc.root", TString outDir = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/export/test", TString outFile = "ndmspc.root", TString objName = "pt_vs_mult", TString contentFileName = "content.root", TString mapStr = "hMap", TString binsDir = "bins", TString fileOpt="?remote=1")
{
  TFile *fIn = TFile::Open(TString::Format("%s%s",input.Data(), fileOpt.Data()).Data(), "READ");
  if (!fIn)
    return 1;

  auto *dir = (TDirectory *)fIn->Get(objName.Data());
  if (!dir)
    return 2;

  dir->ls();

  // mapStr = "hist";

  auto *hMap = (TNamed *)dir->FindObjectAny(mapStr.Data());
  if (!hMap)
    return 3;
  // mapStr = "hMap";
  hMap->SetName(mapStr.Data());

  auto *bins = (TDirectory *)dir->FindObjectAny(binsDir.Data());
  if (!bins)
    return 4;

  TFile *f = nullptr;
  TString path;
  TString binsOutDir = outDir + "/" + binsDir;

  RootDirectorySearchExport(bins, path, f, binsOutDir, contentFileName);

  TFile *fOutMap = TFile::Open(TString::Format("%s/%s%s", outDir.Data(), outFile.Data(), fileOpt.Data()).Data(), "RECREATE");
  if (!fOutMap)
    return 14;

  TDirectory *oDir = fOutMap->mkdir(objName.Data());
  oDir->cd();
  hMap->Write();
  fOutMap->Close();

  fIn->Close();

  Printf("Successfully stored in %s/%s", outDir.Data(), outFile.Data());
  return 0;
}
