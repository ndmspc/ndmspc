#ifndef HnSparseStress_H
#define HnSparseStress_H

#include <TObject.h>
#include <TStopwatch.h>
#include <THnSparse.h>

#include "HnSparse.h"
namespace Ndmspc {
namespace Ndh {

///
/// \class HnSparseStress
///
/// \brief HnSparseStress object
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparseStress : public TObject {

  public:
  HnSparseStress();

  private:
  bool GenerateRecursiveLoop(THnSparse * h, Int_t iDim, Double_t * coord, Int_t * start);
  bool StressRecursiveLoop(HnSparse * h, int & iDim, int * coord);

  public:
  virtual Bool_t Generate(THnSparse * h, Long64_t size = 1e3, Long64_t start = 1e3);
  virtual Bool_t Stress(HnSparse * h, Long64_t size = 1e3, bool bytes = false);

  /// Setting debug level
  void SetDebugLevel(Int_t debug) { fDebugLevel = debug; }
  /// Setting print refresh
  void SetPrintRefresh(Int_t n) { fPrintRefresh = n; }
  /// Setting fill random flag
  void SetRandomFill(bool rf) { fRandomFill = rf; }

  private:
  Long64_t   fNFilledMax{0};      ///< Max size of filled entries
  Long64_t   fNBytesMax{0};       ///< Max size in bytes
  TStopwatch fTimer;              ///< Process timer
  TStopwatch fTimerTotal;         ///< Total timer
  Int_t      fDebugLevel{0};      ///< Debug level
  Int_t      fPrintRefresh{1000}; ///< Print refresh
  bool       fRandomFill{false};  ///< Flag is Fill is random
  bool       fDone{false};        ///< Flag is process is done

  void PrintBin(Int_t n, Double_t * c, const char * msg);

  /// \cond CLASSIMP
  ClassDef(HnSparseStress, 1);
  /// \endcond
};

} // namespace Ndh
} // namespace Ndmspc

#endif /* HnSparseStress_H */
