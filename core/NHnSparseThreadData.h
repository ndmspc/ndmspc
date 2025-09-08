#ifndef Ndmspc_NHnSparseThreadData_H
#define Ndmspc_NHnSparseThreadData_H
#include "NThreadData.h"
#include "NHnSparseBase.h"
#include "Rtypes.h"

namespace Ndmspc {

///
/// \class NHnSparseThreadData
///
/// \brief NHnSparseThreadData object
///	\author Martin Vala <mvala@cern.ch>
///

class NHnSparseThreadData : public NThreadData {
  public:
  NHnSparseThreadData();
  virtual ~NHnSparseThreadData();

  void SetProcessFunc(NHnSparseProcessFuncPtr func) { fProcessFunc = func; }
  // void SetOutput(TList * output) { fOutput = output; }
  void SetBinning(NBinning * binning) { fBinning = binning; }
  void SetBinningDef(NBinningDef * def) { fBinningDef = def; }

  TList * GetOutput() const { return fOutput; }

  virtual void     Process(const std::vector<int> & coords);
  virtual Long64_t Merge(TCollection * list);

  private:
  NHnSparseProcessFuncPtr fProcessFunc{nullptr}; ///< Function pointer to the processing function
  TList *                 fOutput{nullptr};      ///< Global output list for the thread
  NBinning *              fBinning{nullptr};     ///< Binning object for the thread
  NBinningDef *           fBinningDef{nullptr};  ///< Binning definition for the thread

  /// \cond CLASSIMP
  ClassDef(NHnSparseThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
