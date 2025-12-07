#include <TH1F.h>
#include <TH2F.h>
#include <TH3F.h>
#include <TCanvas.h>
#include <TObjArray.h>
#include <TRandom3.h>
#include <TBufferJSON.h>
#include <TROOT.h>
#include "RtypesCore.h"
#include "NWsHandler.h"
#include "NStressHistograms.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NStressHistograms);
/// \endcond

namespace Ndmspc {
NStressHistograms::NStressHistograms(int fill, Long64_t reset, int seed, bool batch)
    : TObject(), fNFill(fill), fReset(reset), fBatch(batch)
{

  // Create some histograms, a profile histogram and an ntuple
  fHpx = new TH1F("hpx", "This is the px distribution", 100, -4, 4);
  fHpx->SetFillColor(48);
  fHpxpy   = new TH2F("hpxpy", "px vs py", 10, -4, 4, 10, -4, 4);
  fHpxpz   = new TH2F("hpxpz", "px vs pz", 10, -4, 4, 10, 0, 10);
  fHpxpypz = new TH3F("hpxpypz", "py vs px vs pz", 10, -4, 4, 10, -4, 4, 10, 0, 10);

  fObjs = new TObjArray();
  fObjs->Add(fHpx);
  fObjs->Add(fHpxpy);
  fObjs->Add(fHpxpz);
  fObjs->Add(fHpxpypz);
  // when read-only mode disabled one could execute object methods like TTree::Draw()

  fRandom.SetSeed(seed); // this is a random seed
}
NStressHistograms::~NStressHistograms() {}
bool NStressHistograms::HandleEvent(NWsHandler * ws)
{

  if (fReset && fNEvents % fReset == 0) {
    fHpx->Reset();
    fHpxpy->Reset();
    fHpxpz->Reset();
    fHpxpypz->Reset();
  }

  // c1->SetFillColor(42);
  // c1->GetFrame()->SetFillColor(21);
  // c1->GetFrame()->SetBorderSize(6);
  // c1->GetFrame()->SetBorderMode(-1);

  // Fill histograms randomly
  Float_t px, py;
  Float_t pz;
  // Long64_t    maxcnt  = 0;
  // const Int_t kUPDATE = 1;
  // const Int_t kSLEEP  = 100;
  // Long64_t i = 0;
  // press Ctrl-C to stop macro
  for (int i = 0; i < fNFill; i++) {

    fRandom.Rannor(px, py);
    pz = px * px + py * py;
    // Float_t rnd = fRandom.Rndm(1);
    fHpx->Fill(px);
    fHpxpy->Fill(px, py);
    fHpxpz->Fill(px, pz);
    fHpxpypz->Fill(px, py, pz);
  }
  // fill only first 25000 events in NTuple
  // if (i == kUPDATE) {
  if (!fBatch) {
    if (!fCanvas) {
      // Create a new canvas.
      fCanvas = new TCanvas("c1", "Dynamic Filling Example", 200, 10, 700, 500);
      fCanvas->Divide(2, 2);
    }
    fCanvas->cd(1);
    fHpx->Draw();
    fCanvas->cd(2);
    fHpxpy->Draw();
    fCanvas->cd(3);
    fHpxpz->Draw();
    fCanvas->cd(4);
    fHpxpypz->Draw();
    fCanvas->ModifiedUpdate();
  }
  fNEvents++;
  Printf("Event %lld fill=%d", fNEvents, fNFill);

  if (ws) {
    ws->Broadcast(TBufferJSON::ConvertToJSON(fObjs).Data());
  }
  return true;
}

} // namespace Ndmspc
