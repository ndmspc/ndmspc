#if defined(__CINT__) || defined(__ROOTCLING__)
#include <TString.h>
#include <TFile.h>
#include <TTree.h>
#include <HepEvent.h>
#include <HepTrack.h>
#endif

void gen(int nEvents = 1000, int nTracks = 100, int refresh = 100, TString filename = "ndhep.root")
{
  Printf("events=%d tracks=%d filename=%s", nEvents, nTracks, filename.Data());

  TFile * f = TFile::Open(filename.Data(), "RECREATE");
  if (!f) {
    Printf("Error: File '%s' cannot be opened !!!", filename.Data());
    return;
  }

  NdmSpc::Hep::Track * t;
  NdmSpc::Hep::Event * ev = new NdmSpc::Hep::Event(0);

  TTree tree("ndhepTree", "Ndhep Tree");
  tree.Branch("Event", &ev);

  for (int iEvent = 0; iEvent < nEvents; ++iEvent) {

    ev->SetID(iEvent);
    ev->BuildVertexRandom();
    for (int iTrack = 0; iTrack < nTracks; ++iTrack) {
      t = ev->AddTrack();
      t->BuildRandom();
    }

    if (iEvent % refresh == 0) ev->Print();
    // Write to file
    tree.Fill();
    ev->Clear();
  }
  f->Write("", TObject::kOverwrite);
  f->Close();

  delete ev;
}
