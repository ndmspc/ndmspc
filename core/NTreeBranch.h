#ifndef Ndmspc_NTreeBranch_H
#define Ndmspc_NTreeBranch_H
#include <TObject.h>
#include <TBranch.h>
#include <string>

namespace Ndmspc {

/**
 * @class NTreeBranch
 * @brief NDMSPC tree branch object for managing ROOT TBranch and associated data.
 *
 * Provides methods for branch creation, address management, status, entry access, and object handling.
 * Supports integration with TTree, TObject, and custom class names.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NTreeBranch : public TObject {
  public:
  /**
   * @brief Constructor.
   * @param tree Pointer to TTree (optional).
   * @param name Branch name (optional).
   * @param address Pointer to branch data (optional).
   * @param objClassName Object class name (default: "TObject").
   */
  NTreeBranch(TTree * tree = nullptr, const std::string & name = "", void * address = nullptr,
              const std::string & objClassName = "TObject");

  /**
   * @brief Destructor.
   */
  virtual ~NTreeBranch();

  /**
   * @brief Print branch information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const;

  /**
   * @brief Set branch name.
   * @param name Branch name.
   */
  void SetName(const std::string & name) { fName = name; }

  /**
   * @brief Set object class name.
   * @param objClassName Object class name.
   */
  void SetObjectClassName(const std::string & objClassName) { fObjectClassName = objClassName; }

  /**
   * @brief Set branch pointer.
   * @param branch Pointer to TBranch.
   */
  void SetBranch(TBranch * branch) { fBranch = branch; }

  /**
   * @brief Set object pointer.
   * @param obj Pointer to TObject.
   */
  void SetObject(TObject * obj) { fObject = obj; }

  /**
   * @brief Get branch pointer.
   * @return Pointer to TBranch.
   */
  TBranch * GetBranch() { return fBranch; }

  /**
   * @brief Get branch status.
   * @return Branch status integer.
   */
  int GetBranchStatus() const { return fBranchStatus; }

  /**
   * @brief Get object pointer.
   * @return Pointer to TObject.
   */
  TObject * GetObject() const { return fObject; }

  /**
   * @brief Get object class name.
   * @return Object class name string.
   */
  std::string GetObjectClassName() const { return fObjectClassName; }

  /**
   * @brief Create branch in TTree with given address.
   * @param tree Pointer to TTree.
   * @param address Pointer to branch data.
   * @return Pointer to created TBranch.
   */
  TBranch * Branch(TTree * tree, void * address);

  /**
   * @brief Set address for branch data.
   * @param address Pointer to branch data.
   * @param deleteExisting If true, delete existing address.
   */
  void SetAddress(void * address, bool deleteExisting = false);

  /**
   * @brief Set branch address in TTree.
   * @param tree Pointer to TTree.
   */
  void SetBranchAddress(TTree * tree);

  /**
   * @brief Set branch status.
   * @param status Status integer to set.
   */
  void SetBranchStatus(int status) { fBranchStatus = status; }

  /**
   * @brief Get entry from TTree.
   * @param tree Pointer to TTree.
   * @param entry Entry index.
   * @return Entry value.
   */
  Long64_t GetEntry(TTree * tree, Long64_t entry);

  /**
   * @brief Save entry to another NTreeBranch.
   * @param hnstIn Pointer to input NTreeBranch.
   * @param useProjection Use projection.
   * @param projOpt Projection options string.
   */
  void SaveEntry(NTreeBranch * hnstIn, bool useProjection = false, const std::string projOpt = "OE");

  private:
  std::string fName{""};                   ///< Branch name
  int         fBranchStatus{1};            ///< Branch status
  TBranch *   fBranch{nullptr};            ///<! Branch pointer
  TObject *   fObject{nullptr};            ///<! Object pointer
  std::string fObjectClassName{"TObject"}; ///< Object class name

  /// \cond CLASSIMP
  ClassDef(NTreeBranch, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
