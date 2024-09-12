#include <TString.h>
#include <TRandom.h>
#include "HepEvent.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::Hep::Event);
/// \endcond

namespace NdmSpc {
namespace Hep {

Event::Event() : TObject(), fID(0), fVx(0.0), fVy(0.0), fVz(0.0), fNTracks(0), fTracks(0)
{
  ///
  /// Default constructor
  ///
}

Event::Event(Long64_t id, Double_t vx, Double_t vy, Double_t vz)
    : TObject(), fID(id), fVx(vx), fVy(vy), fVz(vz), fNTracks(0), fTracks(0)
{
  ///
  /// A constructor
  ///

  fTracks = new TClonesArray("NdmSpc::Hep::Track");
  gRandom->SetSeed(0);
}

Event::~Event()
{
  ///
  /// A destructor
  ///

  delete fTracks;
  fTracks = 0;
}

Track * Event::AddTrack()
{
  ///
  /// Adds track to event
  ///
  return (Track *)fTracks->ConstructedAt(fNTracks++);
}
void Event::Print(Option_t * option) const
{
  ///
  /// Printing event info
  ///
  Printf("id=%lld vx=%.3f vy=%.3f vz=%.3f nTracks=%d ", fID, fVx, fVy, fVz, fNTracks);

  if (!fTracks) return;

  TString str(option);
  str.ToLower();
  if (str.Contains("all")) {
    Track * t;
    for (Int_t i = 0; i < fTracks->GetEntries(); i++) {
      t = (Track *)fTracks->At(i);
      t->Print();
    }
  }
}

void Event::Clear(Option_t *)
{
  ///
  /// Reseting event to default values and clear all tracks
  ///
  fID = 0;
  fVx = 0;
  fVy = 0;
  fVz = 0;

  fNTracks = 0;
  fTracks->Clear("C");
}

void Event::BuildVertexRandom()
{
  ///
  /// Builds random vertex
  ///

  fVx = gRandom->Gaus();
  fVy = gRandom->Gaus();
  fVz = gRandom->Gaus();
}

} // namespace Hep
} // namespace NdmSpc
