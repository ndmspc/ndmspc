#ifndef Ndmspc_NStorageTree_H
#define Ndmspc_NStorageTree_H
#include <TObject.h>
#include <TTree.h>
#include <TFile.h>
#include "NBinning.h"
#include "NBinningPoint.h"
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
  NStorageTree(NBinning * binning = nullptr);
  virtual ~NStorageTree();

  virtual void Print(Option_t * option = "") const;

  /// Tree handling
  bool     SetFileTree(TFile * file, TTree * tree, bool force);
  bool     InitTree(const std::string & filename = "", const std::string & treename = "hnst");
  Long64_t GetEntries() const { return fTree ? fTree->GetEntries() : 0; }
  Long64_t GetEntry(Long64_t entry, NBinningPoint * point = nullptr);
  // Int_t    Fill(NBinningPoint * point);
  Int_t Fill(NBinningPoint * point, NStorageTree * hnstIn = nullptr, std::vector<std::vector<int>> ranges = {},
             bool useProjection = false);
  bool  Close(bool write = false);

  Long64_t Merge(TCollection * list);

  /// Branches handling
  std::vector<std::string>           GetBrancheNames();
  std::map<std::string, NTreeBranch> GetBranchesMap() const { return fBranchesMap; }
  void                               SetBranchesMap(std::map<std::string, NTreeBranch> map) { fBranchesMap = map; }
  bool                               AddBranch(const std::string & name, void * address, const std::string & className);
  NTreeBranch *                      GetBranch(const std::string & name);
  TObject *                          GetBranchObject(const std::string & name);
  void                               SetEnabledBranches(std::vector<std::string> branches);
  void                               SetBranchAddresses();

  NBinning *  GetBinning() const { return fBinning; }
  void        SetBinning(NBinning * binning) { fBinning = binning; }
  TTree *     GetTree() const { return fTree; }
  std::string GetFileName() const { return fFileName; }
  std::string GetPrefix() const { return fPrefix; }
  std::string GetPostfix() const { return fPostfix; }
  void        SetOutputs(TMap * outputs) { fOutputs = outputs; }

  protected:
  std::string                        fFileName{"hnst.root"}; ///< Current filename
  TFile *                            fFile{nullptr};         ///<! Current file
  TTree *                            fTree{nullptr};         ///<! TTree container
  std::string                        fPrefix{""};            ///< Prefix path
  std::string                        fPostfix{""};           ///< Postfix path
  std::map<std::string, NTreeBranch> fBranchesMap;           ///< Branches map
  TMap *                             fOutputs;               ///<! Output objects map
  NBinning *                         fBinning{nullptr};      ///! Binning object

  /// \cond CLASSIMP
  ClassDef(NStorageTree, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
