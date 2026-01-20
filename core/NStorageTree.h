#ifndef Ndmspc_NStorageTree_H
#define Ndmspc_NStorageTree_H
#include <TObject.h>
#include <TTree.h>
#include <TFile.h>
#include "NBinning.h"
#include "NBinningPoint.h"
#include "NTreeBranch.h"

namespace Ndmspc {

/**
 * @class NStorageTree
 * @brief NDMSPC storage tree object for managing ROOT TTree-based data storage.
 *
 * Provides methods for tree/file handling, branch management, binning integration, entry access,
 * filling, merging, and output management. Supports flexible branch enabling, address setting,
 * and integration with NBinning and NBinningPoint.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NStorageTree : public TObject {
  public:
  /**
   * @brief Constructor.
   * @param binning Pointer to NBinning object (optional).
   */
  NStorageTree(NBinning * binning = nullptr);

  /**
   * @brief Destructor.
   */
  virtual ~NStorageTree();

  /**
   * @brief Print storage tree information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const;

  /**
   * @brief Clear storage tree data.
   * @param option Clear options.
   */
  virtual void Clear(Option_t * option = "");

  /// Tree handling

  /**
   * @brief Set file and tree for storage.
   * @param file Pointer to TFile.
   * @param tree Pointer to TTree.
   * @return True if set successfully.
   */
  bool SetFileTree(TFile * file, TTree * tree);

  /**
   * @brief Initialize tree from file and tree name.
   * @param filename File name (optional).
   * @param treename Tree name (default: "ngnt").
   * @return True if initialized successfully.
   */
  bool InitTree(const std::string & filename = "", const std::string & treename = "ngnt");

  /**
   * @brief Get number of entries in the tree.
   * @return Number of entries.
   */
  Long64_t GetEntries() const { return fTree ? fTree->GetEntries() : 0; }

  /**
   * @brief Get entry by index and fill NBinningPoint.
   * @param entry Entry index.
   * @param point Pointer to NBinningPoint (optional).
   * @param checkBinningDef Check binning definition.
   * @return Status code.
   */
  Long64_t GetEntry(Long64_t entry, NBinningPoint * point = nullptr, bool checkBinningDef = false);

  // Int_t    Fill(NBinningPoint * point);

  /**
   * @brief Fill tree with NBinningPoint and optional input tree.
   * @param point Pointer to NBinningPoint.
   * @param hnstIn Pointer to input NStorageTree (optional).
   * @param ignoreFilledCheck Ignore filled check.
   * @param ranges Ranges for axes.
   * @param useProjection Use projection.
   * @return Status code.
   */
  Int_t Fill(NBinningPoint * point, NStorageTree * hnstIn = nullptr, bool ignoreFilledCheck = false,
             std::vector<std::vector<int>> ranges = {}, bool useProjection = false);

  /**
   * @brief Close the storage tree, optionally writing outputs.
   * @param write If true, write outputs before closing.
   * @param outputs Map of output names to TList pointers.
   * @return True if closed successfully.
   */
  bool Close(bool write = false, std::map<std::string, TList *> outputs = {});

  /**
   * @brief Returns the associated TFile object.
   *
   * @return Pointer to the TFile associated with this storage tree.
   */
  TFile * GetFile() const { return fFile; }

  /**
   * @brief Merge storage trees from a collection.
   * @param list Pointer to TCollection.
   * @return Number of merged entries.
   */
  Long64_t Merge(TCollection * list);

  /// Branches handling

  /**
   * @brief Get names of branches.
   * @param onlyEnabled If true, return only enabled branches.
   * @return Vector of branch names.
   */
  std::vector<std::string> GetBrancheNames(bool onlyEnabled = false) const;

  /**
   * @brief Get map of branch names to NTreeBranch objects.
   * @return Map of branch names to NTreeBranch.
   */
  std::map<std::string, NTreeBranch> GetBranchesMap() const { return fBranchesMap; }

  /**
   * @brief Set map of branch names to NTreeBranch objects.
   * @param map Map of branch names to NTreeBranch.
   */
  void SetBranchesMap(std::map<std::string, NTreeBranch> map) { fBranchesMap = map; }

  /**
   * @brief Add a branch to the tree.
   * @param name Branch name.
   * @param address Pointer to branch data.
   * @param className Class name of branch data.
   * @return True if added successfully.
   */
  bool AddBranch(const std::string & name, void * address, const std::string & className);

  /**
   * @brief Get pointer to NTreeBranch by name.
   * @param name Branch name.
   * @return Pointer to NTreeBranch.
   */
  NTreeBranch * GetBranch(const std::string & name);

  /**
   * @brief Get pointer to branch object by name.
   * @param name Branch name.
   * @return Pointer to TObject.
   */
  TObject * GetBranchObject(const std::string & name);

  /**
   * @brief Set enabled/disabled status for branches.
   * @param branches Vector of branch names.
   * @param status Status to set (1=enabled, 0=disabled).
   */
  void SetEnabledBranches(std::vector<std::string> branches, int status = 1);

  /**
   * @brief Set addresses for all branches.
   */
  void SetBranchAddresses();

  /**
   * @brief Get pointer to binning object.
   * @return Pointer to NBinning.
   */
  NBinning * GetBinning() const { return fBinning; }

  /**
   * @brief Set binning object pointer.
   * @param binning Pointer to NBinning.
   */
  void SetBinning(NBinning * binning) { fBinning = binning; }

  /**
   * @brief Get pointer to TTree object.
   * @return Pointer to TTree.
   */
  TTree * GetTree() const { return fTree; }

  /**
   * @brief Get file name.
   * @return File name string.
   */
  std::string GetFileName() const { return fFileName; }

  /**
   * @brief Get prefix path.
   * @return Prefix string.
   */
  std::string GetPrefix() const { return fPrefix; }

  /**
   * @brief Get postfix path.
   * @return Postfix string.
   */
  std::string GetPostfix() const { return fPostfix; }

  /**
   * @brief Set outputs map.
   * @param outputs Pointer to TMap of outputs.
   */
  void SetOutputs(TMap * outputs) { fOutputs = outputs; }

  protected:
  std::string                        fFileName{"ngnt.root"}; ///< Current filename
  TFile *                            fFile{nullptr};         ///<! Current file
  TTree *                            fTree{nullptr};         ///<! TTree container
  std::string                        fPrefix{""};            ///< Prefix path
  std::string                        fPostfix{""};           ///< Postfix path
  std::map<std::string, NTreeBranch> fBranchesMap;           ///< Branches map
  TMap *                             fOutputs;               ///<! Output objects map
  NBinning *                         fBinning{nullptr};      ///<-> Binning object

  /// \cond CLASSIMP
  ClassDef(NStorageTree, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
