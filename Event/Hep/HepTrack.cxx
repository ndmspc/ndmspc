#include <TString.h>
#include <TRandom.h>
#include <TMath.h>
#include "HepTrack.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::Hep::Track);
/// \endcond

namespace NdmSpc {
namespace Hep {

Track::Track() : TObject(), fPx(0.0), fPy(0.0), fPz(0.0), fCharge(0), fIsPrimary(0), fTPCSignal(0)
{
  ///
  /// A constructor
  ///

  gRandom->SetSeed(0);
  for (Int_t i = 0; i < 5; ++i) {
    fPIDNsigma[i] = -999.0;
  }
}

Track::~Track()
{
  ///
  /// A destructor
  ///
}

void Track::BuildRandom()
{
  ///
  /// Building random event
  ///

  Double_t px, py;
  gRandom->Rannor(px, py);
  fPx = px;
  fPy = py;
  fPz = TMath::Sqrt(px * px + py * py);

  fIsPrimary = (gRandom->Integer(2) > 0) ? kTRUE : kFALSE;

  // Generate charge
  fCharge = (gRandom->Integer(2) > 0) ? 1 : -1;
}

void Track::Print(Option_t * /*option*/) const
{
  ///
  /// Printing track info
  ///

  Printf("ch=%d px=%.3f py=%.3f pz=%.3f primary=%d", fCharge, fPx, fPy, fPz, fIsPrimary);
}

void Track::Clear(Option_t *)
{
  ///
  /// Reseting track to default values
  ///

  fCharge    = 0;
  fPx        = 0;
  fPy        = 0;
  fPz        = 0;
  fTPCSignal = 0;
  fIsPrimary = kFALSE;

  for (Int_t i = 0; i < 5; ++i) {
    fPIDNsigma[i] = -999.0;
  }
}

void Track::SetP(Double_t * p)
{
  ///
  /// Sets all components of momentum
  ///
  fPx = p[0];
  fPy = p[1];
  fPz = p[2];
}

Double_t Track::GetP() const
{
  ///
  /// Returns value of total momentum for current track
  ///
  return TMath::Sqrt(TMath::Power(fPx, 2) + TMath::Power(fPy, 2) + TMath::Power(fPz, 2));
}

void Track::SetPIDNsigma(Int_t i, Double_t s)
{
  ///
  /// Sets all components of nsigma
  /// (kElectron kMuon kPion kKaon kProton)
  ///

  fPIDNsigma[i] = s;
}
} // namespace Hep
} // namespace NdmSpc
