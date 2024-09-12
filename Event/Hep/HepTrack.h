#ifndef HepTrack_H
#define HepTrack_H

#include <TObject.h>
namespace NdmSpc {
namespace Hep {

///
/// \class Track
///
/// \brief Track object
///	\author Martin Vala <mvala@cern.ch>
///

class Track : public TObject {

  public:
  Track();
  virtual ~Track();

  /// \fn Double_t GetPx() const
  /// Momentum x component
  /// \return Px component
  ///! \fn Double_t GetPy() const
  /// Momentum y component
  /// \return Py component
  ///! \fn Double_t GetPz() const
  /// Momentum z component
  ///\return Pz component
  ///! \fn Int_t GetCharge() const
  /// Charge of track
  ///\return charge
  ///! \fn Int_t GetTPCSignal() const
  /// TPC signal of track
  ///\return tpcSignal
  ///! \fn Int_t GetPIDNsigma(Int_t i) const
  /// NSigma of PID type
  ///\return pidNsigma
  ///! \fn Bool_t IsPrimary() const
  /// Is track primary
  ///\return primary
  ///! \fn Double_t GetP() const
  /// Get momentum value for current track

  ///! \fn void SetPx(Double_t px)
  /// \param px Momentum x component
  /// Sets x component of momentum
  ///! \fn void SetPy(Double_t py)
  /// \param py Momentum y component
  /// Sets y component of momentum
  ///! \fn void SetPz(Double_t pz)
  /// \param pz Momentum z component
  /// Sets z component of momentum
  ///! \fn void SetCharge(Int_t ch)
  /// \param ch Charge
  /// Sets charge
  ///! \fn void SetTPCSignal(Double_t s)
  /// \param s TPC signal
  /// Sets TPC signal
  ///! \fn void SetPIDNsigma(Int_t i, Double_t s)
  /// \param i particle type
  /// \param s nSigma value
  /// Sets PID nSigma value
  ///! \fn void SetPrimary(Bool_t isPrimary)
  /// \param isPrimary is track primary
  /// Sets if track is primary

  Double_t GetPx() const { return fPx; }
  Double_t GetPy() const { return fPy; }
  Double_t GetPz() const { return fPz; }
  Short_t  GetCharge() const { return fCharge; }
  Double_t GetTPCSignal() const { return fTPCSignal; }
  Double_t GetPIDNsigma(Int_t i) const { return fPIDNsigma[i]; }
  Bool_t   IsPrimary() const { return fIsPrimary; }
  Double_t GetP() const;

  void SetPx(Double_t px) { fPx = px; }
  void SetPy(Double_t py) { fPy = py; }
  void SetPz(Double_t pz) { fPz = pz; }
  void SetP(Double_t * p);
  void SetTPCSignal(Double_t s) { fTPCSignal = s; }
  void SetCharge(Short_t ch) { fCharge = ch; }
  void SetPIDNsigma(Int_t i, Double_t s);
  void SetPrimary(Bool_t isPrimary) { fIsPrimary = isPrimary; }

  virtual void Print(Option_t * option = "") const;
  virtual void Clear(Option_t * option = "");

  void BuildRandom();

  private:
  Double_t fPx;           ///< Momentum x
  Double_t fPy;           ///< Momentum y
  Double_t fPz;           ///< Momentum z
  Short_t  fCharge;       ///< Charge
  Bool_t   fIsPrimary;    ///< Flag if track was defined as primary
  Double_t fTPCSignal;    ///< TPC signal
  Double_t fPIDNsigma[5]; ///< PID N Sigma

  // TODO
  /// Copy constructor
  Track(const Track &);             /// not implemented
  Track & operator=(const Track &); /// not implemented

  /// \cond CLASSIMP
  ClassDef(Track, 1);
  /// \endcond
};
} // namespace Hep
} // namespace NdmSpc
#endif /* HepTrack_H */
