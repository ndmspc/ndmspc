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

  virtual void     Process(const std::vector<int> & coords);
  virtual Long64_t Merge(TCollection * list);

  private:
  NHnSparseProcessFuncPtr fProcessFunc{nullptr};  ///< Function pointer to the processing function
  TList *                 fOutputGlobal{nullptr}; ///< Global output list for the thread

  /// \cond CLASSIMP
  ClassDef(NHnSparseThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
