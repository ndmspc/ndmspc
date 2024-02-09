#include <TFile.h>
#include <TH2D.h>
#include <TH3D.h>
#include <TKey.h>
#include <TObjArray.h>
#include <TObjString.h>
#include <TString.h>
#include <TSystem.h>
#include <TUrl.h>
#include <sstream>

using namespace std;

std::vector<Int_t> ParseListOfBins(const char * str, char token = '/')
{
  std::vector<Int_t> result;
  do {
    const char * begin = str;
    while (*str != token && *str) str++;
    std::string s = std::string(begin, str);
    if (!s.empty()) result.push_back(atoi(s.c_str()));
  } while (0 != *str++);

  return result;
}

Int_t NdmspcScanProjectionMap(TString hProjMapDir = "root://eos.ndmspc.io///eos/ndmspc/scratch/ndmspc/demo/phi/axis1-pt_axis2-m",
                              Double_t binContent = 2, Double_t defaultFill = 1, TString projMap = "hProjMap",
                              TString contentFile = "content.root", TString binsDir = "bins",
                              TString fileOpt = "?remote=1")
{
  TH1::AddDirectory(kFALSE);

  TString hProjMapFile = TString::Format("%s/%s.root", hProjMapDir.Data(), projMap.Data());

  TUrl    url(hProjMapFile);
  TString outHost        = url.GetHost();
  TString inputDirectory = gSystem->DirName(url.GetFile());

  TString findUrl = TString::Format(
      "root://%s//proc/user/?mgm.cmd=find&mgm.find.match=%s&mgm.path=%s/%s&mgm.format=json&mgm.option=f&filetype=raw",
      outHost.Data(), contentFile.Data(), inputDirectory.Data(), binsDir.Data());

  TFile * f = TFile::Open(findUrl.Data());
  if (!f) return 1;

  // Printf("%lld", f->GetSize());

  int  buffsize = 4096;
  char buff[buffsize + 1];

  Long64_t buffread = 0;
  string   content;
  while (buffread < f->GetSize()) {

    if (buffread + buffsize > f->GetSize()) buffsize = f->GetSize() - buffread;

    // Printf("Buff %lld %d", buffread, buffsize);
    f->ReadBuffer(buff, buffread, buffsize);
    buff[buffsize] = '\0';
    content += buff;
    buffread += buffsize;
  }

  f->Close();

  string ss  = "mgm.proc.stdout=";
  size_t pos = ss.size() + 1;
  content    = content.substr(pos);

  // Vector of string to save tokens
  vector<string> tokens;

  // stringstream class check1
  stringstream check1(content);

  string intermediate;

  // Tokenizing w.r.t. space '&'
  while (getline(check1, intermediate, '&')) {
    tokens.push_back(intermediate);
  }

  stringstream check2(tokens[0]);
  string       line;

  TFile * fIn = TFile::Open(hProjMapFile.Data());
  if (!fIn) return 2;

  TKey * k = fIn->GetKey(projMap.Data());
  if (!k) return 3;
  TString className = k->GetClassName();
  // // k->Print();
  TH1 * hProjMap;
  hProjMap = (TH1 *)fIn->Get(projMap.Data())->Clone();

  if (!hProjMap) return 4;

  fIn->Close();

  hProjMap->Reset();
  hProjMap->SetStats(kFALSE);

  if (className.BeginsWith("TH1")) {
    hProjMap->SetFillColor(kYellow);
    for (Int_t i = 0; i < hProjMap->GetXaxis()->GetNbins(); i++) {
      hProjMap->SetBinContent(i + 1, defaultFill);
    }
  }

  else if (className.BeginsWith("TH2")) {
    for (Int_t i = 0; i < hProjMap->GetXaxis()->GetNbins(); i++)
      for (Int_t j = 0; j < hProjMap->GetYaxis()->GetNbins(); j++) {
        hProjMap->SetBinContent(i + 1, j + 1, defaultFill);
      }
  }
  else if (className.BeginsWith("TH3")) {
    for (Int_t i = 0; i < hProjMap->GetXaxis()->GetNbins(); i++)
      for (Int_t j = 0; j < hProjMap->GetYaxis()->GetNbins(); j++)
        for (Int_t k = 0; k < hProjMap->GetZaxis()->GetNbins(); k++) {
          hProjMap->SetBinContent(i + 1, j + 1, k + 1, defaultFill);
        }
  }

  Int_t   bin[3]             = {0, 0, 0};
  TString inputBinsDirectory = TString::Format("%s/%s/", inputDirectory.Data(), binsDir.Data());
  contentFile                = TString::Format("/%s", contentFile.Data());
  while (getline(check2, line)) {
    TString strPath(line);
    strPath = strPath.ReplaceAll(inputBinsDirectory.Data(), "");
    strPath = strPath.ReplaceAll(contentFile.Data(), "");

    // Printf("%s %s", strPath.Data(), inputBinsDirectory.Data());
    std::vector<Int_t> bins = ParseListOfBins(strPath.Data());

    // Printf("size=%zu ", bins.size());
    Int_t i = 0;
    for (auto & b : bins) {
      // printf("%d ", b);
      bin[i++] = b;
    }
    Printf("Found bin: %d %d %d", bin[0], bin[1], bin[2]);
    hProjMap->SetBinContent(bin[0], bin[1], bin[2], binContent);
  }

  hProjMap->SetMinimum(0);
  hProjMap->SetMaximum(binContent);

  TFile * fOut = TFile::Open(TString::Format("%s%s", hProjMapFile.Data(), fileOpt.Data()), "RECREATE");
  if (!fOut) return 5;
  hProjMap->Write();

  fOut->Close();

  Printf("File '%s' was modified", hProjMapFile.Data());
  return 0;
}
