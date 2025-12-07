#ifndef NdmspcNHnSparseTreeInfo_H
#define NdmspcNHnSparseTreeInfo_H
#include <TNamed.h>
#include "NHnSparseTree.h"

namespace Ndmspc {

///
/// \class NHnSparseTreeInfo
///
/// \brief NHnSparseTreeInfo object
///	\author Martin Vala <mvala@cern.ch>
///

class NHnSparseTreeInfo : public TNamed {
  public:
  NHnSparseTreeInfo(const char * name = "hnstInfo", const char * title = "NHnSparseTreeInfo");
  virtual ~NHnSparseTreeInfo();

  virtual void Print(Option_t * option = "") const;

  ///< Set HnSparseTree
  void SetHnSparseTree(NHnSparseTree * ngnt) { fHnSparseTree = ngnt; }
  ///< Getter for HnSparseTree
  NHnSparseTree * GetHnSparseTree() const { return fHnSparseTree; }

  private:
  NHnSparseTree * fHnSparseTree{nullptr}; ///< HnSparseTree

  /// \cond CLASSIMP
  ClassDef(NHnSparseTreeInfo, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
