#ifndef HepEvent_H
#define HepEvent_H

#include <TObject.h>
#include <TClonesArray.h>
#include "HepTrack.h"

namespace NdmSpc {
namespace Hep {
///
/// \class Event
///
/// \brief Event object
/// \author Martin Vala <mvala@cern.ch>
///

class Event : public TObject {

  public:
  Event();
  Event(Long64_t id, Double_t vx = 0.0, Double_t vy = 0.0, Double_t vz = 0.0);
  virtual ~Event();

  /// \fn Long64_t GetID() const
  /// Event ID
  /// \return event ID
  ///
  /// \fn Double_t GetVx() const
  /// Vertex x component
  /// \return Vx component
  ///
  /// \fn Double_t GetVy() const
  /// Vertex y component
  /// \return Vy component
  ///
  /// \fn Double_t GetVz() const
  /// Vertex z component
  /// \return Vz component
  ///
  /// \fn void SetID(Long64_t id)
  /// \param id Event ID
  ///
  /// Sets event ID
  ///
  /// \fn void SetVx(Double_t vx)
  /// \param vx Vertex x component
  ///
  /// Sets x component of vertex
  /// \fn void SetVy(Double_t vy)
  /// \param vy Vertex x component
  ///
  /// Sets y component of vertex
  ///
  /// \fn void SetVz(Double_t vz)
  /// \param vz Vertex x component
  ///
  /// Sets z component of vertex
  ///
  /// \fn Long64_t GetNTrack() const
  /// \return number of tracks
  ///
  /// \fn Track *GetTrack(Long64_t id)
  /// \param id Track ID
  /// \return Track with id

  Long64_t GetID() const { return fID; }
  Double_t GetVx() const { return fVx; }
  Double_t GetVy() const { return fVy; }
  Double_t GetVz() const { return fVz; }

  void SetID(Long64_t id) { fID = id; }
  void SetVx(Double_t vx) { fVx = vx; }
  void SetVy(Double_t vy) { fVy = vy; }
  void SetVz(Double_t vz) { fVz = vz; }

  Long64_t GetNTrack() const { return fNTracks; }
  Track *  GetTrack(Long64_t id) { return (Track *)fTracks->At(id); }
  Track *  AddTrack();

  virtual void Print(Option_t * option = "") const;
  virtual void Clear(Option_t * option = "");

  void BuildVertexRandom();

  private:
  Long64_t fID;      ///< ID of event
  Double_t fVx;      ///< Vertex x
  Double_t fVy;      ///< Vertex y
  Double_t fVz;      ///< Vertex z
  Int_t    fNTracks; ///< Number of tracks

  /// Array with all tracks
  TClonesArray * fTracks; //->

  // TODO
  /// Copy constructor
  Event(const Event &);             /// not implemented
  Event & operator=(const Event &); /// not implemented

  /// \cond CLASSIMP
  ClassDef(Event, 1);
  /// \endcond
};

} // namespace Hep
} // namespace NdmSpc
#endif /* HepEvent_H */
