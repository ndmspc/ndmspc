#if defined(__CINT__) || defined(__ROOTCLING__)
#include <TString.h>
#include <TFile.h>
#include <TChain.h>
#include <TMath.h>
#include <TH1D.h>
#include <HepEvent.h>
#include <HepTrack.h>
#endif

// eos -b find -f -name "ndhep.root" --xurl /eos/alike.saske.sk/scratch/gp/test/

TChain * GetChain(TString fname);

void ana(TString input = "ndhep.root", TString filenameOut = "out.root", int refresh = 10)
{

  TChain * ch = nullptr;
  if (input.EndsWith(".txt")) {
    ch = GetChain(input);
  }
  else {
    ch = new TChain("ndhepTree");
    ch->AddFile(input.Data());
  }
  if (!ch) return;

  Ndmspc::Hep::Event * ev = nullptr;
  ch->SetBranchAddress("Event", &ev);

  Ndmspc::Hep::Track * t;
  TH1D *               hVx = new TH1D("hVx", "Vx distribution", 200, -10, 10);
  TH1D *               hPx = new TH1D("hPx", "Px distribution", 100, 0, 10);
  TH1D *               hPy = new TH1D("hPy", "Py distribution", 100, 0, 10);
  TH1D *               hPt = new TH1D("hPt", "Pt distribution", 100, 0, 10);
  Double_t             px, py, pt, vx;
  for (int iEvent = 0; iEvent < ch->GetEntries(); ++iEvent) {
    ch->GetEntry(iEvent);
    if (iEvent % refresh == 0) ev->Print();

    vx = ev->GetVx();
    hVx->Fill(vx);

    for (int iTrack = 0; iTrack < ev->GetNTrack(); ++iTrack) {
      t  = ev->GetTrack(iTrack);
      px = t->GetPx();
      py = t->GetPy();
      pt = TMath::Sqrt(TMath::Power(px, 2) + TMath::Power(py, 2));
      hPt->Fill(pt);
      hPx->Fill(px);
      hPy->Fill(py);
    }
  }
  hVx->Draw();
  // hPx->Draw();
  // hPy->Draw();
  // hPt->Draw();

  TFile * fOut = TFile::Open(filenameOut.Data(), "RECREATE");
  hVx->Write();
  hPx->Write();
  hPy->Write();
  hPt->Write();
  fOut->Close();
}

TChain * GetChain(TString fname)
{
  TChain * ch = new TChain("ndhepTree");

  string   line;
  ifstream f(fname.Data());
  if (f.is_open()) {
    while (getline(f, line)) {
      ch->AddFile(TString::Format("%s", line.data()).Data());
    }
    f.close();
  }

  return ch;
}
