#ifndef NdmspcHnSparseTreeInfo_H
#define NdmspcHnSparseTreeInfo_H
#include <TNamed.h>
#include "HnSparseTree.h"

namespace Ndmspc {

///
/// \class HnSparseTreeInfo
///
/// \brief HnSparseTreeInfo object
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparseTreeInfo : public TNamed {
  public:
  HnSparseTreeInfo(const char * name = "hnstInfo", const char * title = "HnSparseTreeInfo");
  virtual ~HnSparseTreeInfo();

  virtual void Print(Option_t * option = "") const;

  ///< Set HnSparseTree
  void SetHnSparseTree(HnSparseTree * hnst) { fHnSparseTree = hnst; }
  ///< Getter for HnSparseTree
  HnSparseTree * GetHnSparseTree() const { return fHnSparseTree; }

  private:
  HnSparseTree * fHnSparseTree{nullptr}; ///< HnSparseTree

  /// \cond CLASSIMP
  ClassDef(HnSparseTreeInfo, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
