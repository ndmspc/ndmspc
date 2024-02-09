#include <TString.h>
#include <TFile.h>
#include <TKey.h>
#include <sstream>
using namespace std;
Int_t NdmspcDirectoryToFile(TString inputDirectory = "/eos/ndmspc/scratch/ndmspc/export/test", TString eosHost = "eos.ndmspc.io", TString output = "root://eos.ndmspc.io//eos/ndmspc/scratch/out2.root", TString objName = "pt_vs_mult", TString inputFile = "ndmspc.root", TString binsDir = "bins", TString mapStr = "hMap",TString contentFile="content.root", TString fileOpt="?remote=1")
{

  TString url = TString::Format("root://%s//proc/user/?mgm.cmd=find&mgm.find.match=%s&mgm.path=%s/%s&mgm.format=json&mgm.option=f&filetype=raw", eosHost.Data(), contentFile.Data(), inputDirectory.Data(), binsDir.Data());

  // rm the file
  // TFile::Open("root://eos.ndmspc.io//proc/user/?mgm.cmd=rm&mgm.path=/eos/ndmspc/scratch/ndmspc/export/test/bins/19/5/content.root&mgm.option=f&filetype=raw")

  TFile *f = TFile::Open(url.Data());
  if (!f)
    return 1;

  Printf("%lld", f->GetSize());

  int buffsize = 4096;
  char buff[buffsize + 1];

  Long64_t buffread = 0;
  string content;
  while (buffread < f->GetSize())
  {

    if (buffread + buffsize > f->GetSize())
      buffsize = f->GetSize() - buffread;

    Printf("Buff %lld %d", buffread, buffsize);
    f->ReadBuffer(buff, buffread, buffsize);
    buff[buffsize] = '\0';
    content += buff;
    buffread += buffsize;
  }

  string ss = "mgm.proc.stdout=";
  size_t pos = ss.size() + 1;
  content = content.substr(pos);

  // Vector of string to save tokens
  vector<string> tokens;

  // stringstream class check1
  stringstream check1(content);

  string intermediate;

  // Tokenizing w.r.t. space '&'
  while (getline(check1, intermediate, '&'))
  {
    tokens.push_back(intermediate);
  }

  TFile *fNdmspcInput = TFile::Open(TString::Format("root://%s/%s/%s", eosHost.Data(), inputDirectory.Data(), inputFile.Data()).Data(), "READ");
  if (!fNdmspcInput)
    return 2;

  auto *fNdmspcInputDirectory = (TDirectory *)fNdmspcInput->Get(objName.Data());

  auto *hMap = (TNamed *)fNdmspcInputDirectory->Get(mapStr.Data());
  if (!hMap)
    return 3;
  hMap->Print();

  TFile *fOutput = TFile::Open(TString::Format("%s%s",output.Data(),fileOpt.Data()).Data(), "RECREATE");
  if (!fOutput)
    return 14;

  TDirectory *oDir = fOutput->mkdir(objName.Data());
  oDir->cd();

  hMap->Write();
  fOutput->ls();


  TDirectory *bDir = oDir->mkdir(binsDir.Data());
  oDir->cd();
  oDir->ls();
  // return 0;

  stringstream check2(tokens[0]);
  string line;
  TString strToRemove = TString::Format("%s/%s", inputDirectory.Data(), binsDir.Data()).Data();
  while (getline(check2, line))
  {
    TString strPath(line);
    Printf("File:  root://%s/%s", eosHost.Data(), line.c_str());
    TFile *fBin = TFile::Open(TString::Format("root://%s/%s", eosHost.Data(), line.c_str()).Data(), "READ");

    // fBin->ls();
    strPath.ReplaceAll(strToRemove.Data(), "");
    strPath.ReplaceAll("content.root", "");
    strPath.Remove(0,1);


    Printf("Creating : %s", strPath.Data());

    TList *keys = fBin->GetListOfKeys();
    // keys->Print();

    bDir->mkdir(strPath.Data(),"", kTRUE);
    TDirectory *currentBinDir = bDir->GetDirectory(strPath.Data());
    // currentBinDir->Cd(strPath.Data());
    Printf("Path %s", currentBinDir->GetPath());

    for (Int_t i = 0; i < keys->GetEntries(); i++)
    {
      Printf("%s %s", strPath.Data(), keys->At(i)->GetName());
      // currentBinDir->cd();
      TObject* o = ((TKey*)keys->At(i))->ReadObj();
      currentBinDir->cd();
      o->Write();
      // fBin->cd();
    }

    fBin->Close();
  }

  fOutput->Close();

  Printf("Successfully stored in %s", output.Data());
  f->Close();
  return 0;
}
