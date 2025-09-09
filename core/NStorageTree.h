#ifndef Ndmspc_NStorageTree_H
#define Ndmspc_NStorageTree_H
#include <TObject.h>
#include <TTree.h>
#include <TFile.h>
#include "NTreeBranch.h"

namespace Ndmspc {

///
/// \class NStorageTree
///
/// \brief NStorageTree object
///	\author Martin Vala <mvala@cern.ch>
///
class NStorageTree : public TObject {
  public:
  NStorageTree();
  virtual ~NStorageTree();

  protected:
  std::string fFileName{"hnst.root"}; ///< Current filename
  TFile *     fFile{nullptr};         ///<! Current file
  // std::string fPrefix{""};            ///< Prefix path
  // std::string fPostfix{""};           ///< Postfix path
  TTree *                            fTree{nullptr}; ///<! TTree container
  std::map<std::string, NTreeBranch> fBranchesMap;   ///< Branches map

  /// \cond CLASSIMP
  ClassDef(NStorageTree, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
