#ifndef Ndmspc_NHnSparseThreadData_H
#define Ndmspc_NHnSparseThreadData_H
// #include "NBinning.h"
// #include "NStorageTree.h"
#include "NGnTree.h"
#include "NThreadData.h"
#include <unordered_set>

namespace Ndmspc {

/**
 * @class NGnThreadData
 * @brief Thread-local data object for NDMSPC processing.
 *
 * Manages per-thread processing function, input/output trees, binning, configuration,
 * and processed entry count. Supports initialization, storage setup, processing, and merging.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NGnThreadData : public NThreadData {
  public:
  /**
   * @brief Constructor.
   */
  NGnThreadData();

  /**
   * @brief Destructor.
   */
  virtual ~NGnThreadData();

  /**
   * @brief Initialize thread data for processing.
   * @param id Thread ID.
   * @param func Processing function pointer.
   * @param ngnt Pointer to NGnTree object.
   * @param binningIn Pointer to NBinning object.
   * @param input Optional input NGnTree pointer.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "ngnt").
   * @return True if initialization successful.
   */
  bool Init(size_t id, NGnProcessFuncPtr func, NGnBeginFuncPtr beginFunc, NGnEndFuncPtr endFunc, NGnTree * ngnt,
            NBinning * binningIn, NGnTree * input = nullptr, const std::string & filename = "",
            const std::string & treename = "ngnt");

  /**
   * @brief Set the processing function pointer.
   * @param func Function pointer to set.
   */
  void SetProcessFunc(NGnProcessFuncPtr func) { fProcessFunc = func; }

  /**
   * @brief Initialize storage tree for thread data.
   * @param ts Pointer to NStorageTree.
   * @param filename Optional file name.
   * @param treename Optional tree name (default: "ngnt").
   * @return True if storage initialized successfully.
   */
  bool InitStorage(NStorageTree * ts, const std::string & filename = "", const std::string & treename = "ngnt");

  /**
   * @brief Get pointer to base NGnTree object.
   * @return Pointer to NGnTree.
   */
  NGnTree * GetHnSparseBase() const { return fHnSparseBase; }

  /**
   * @brief Get number of processed entries.
   * @return Number of processed entries.
   */
  Long64_t GetNProcessed() const { return fNProcessed; }

  /**
   * @brief Set number of processed entries.
   * @param n Number of processed entries.
   */
  void SetNProcessed(Long64_t n) { fNProcessed = n; }

  /**
   * @brief Set configuration JSON object.
   * @param cfg JSON configuration.
   */
  void SetCfg(const json & cfg) { fCfg = cfg; }

  /**
   * @brief Get configuration JSON object.
   * @return JSON configuration.
   */
  json GetCfg() const { return fCfg; }

  /**
   * @brief Set the results filename for TCP mode (shared filesystem path).
   *
   * When set, TaskLoop will copy the locally-written file to this path after
   * Close(true) before sending DONE to the supervisor.
   * @param filename Shared-filesystem path where results should be deposited.
   */
  void SetResultsFilename(const std::string & filename) { fResultsFilename = filename; }

  /**
   * @brief Get the results filename for TCP mode.
   * @return Shared-filesystem path, or empty string if not set.
   */
  const std::string & GetResultsFilename() const { return fResultsFilename; }

  /**
   * @brief Checks if the object is a pure copy.
   * @return True if the object is a pure copy, false otherwise.
   */
  bool IsPureCopy() const { return fIsPureCopy; }

  /**
   * @brief Sets the pure copy status of the object.
   * @param val Boolean value to set the pure copy status.
   */
  void SetIsPureCopy(bool val) { fIsPureCopy = val; }

  /**
   * @brief Process coordinates (virtual).
   * @param coords Vector of coordinates.
   */
  virtual void Process(const std::vector<int> & coords);

  void SetCurrentDefinitionName(const std::string & name);
  void SyncCurrentDefinitionIds(const std::vector<Long64_t> & ids);

  void ExecuteBeginFunction();
  void ExecuteEndFunction();

  /**
   * @brief Delete deferred ROOT objects, skipping TCanvas/TPad (leaked safely).
   */
  void FlushDeferredDeletes();

  /**
   * @brief Merge thread data from a collection (virtual).
   * @param list Pointer to TCollection.
   * @return Number of merged entries.
   */
  virtual Long64_t Merge(TCollection * list);

  private:
  NGnProcessFuncPtr          fProcessFunc{nullptr};  ///< Function pointer to the processing function
  NGnBeginFuncPtr            fBeginFunc{nullptr};    ///< Function pointer to the begin function
  NGnEndFuncPtr              fEndFunc{nullptr};      ///< Function pointer to the end function
  NGnTree *                  fHnSparseBase{nullptr}; ///< Pointer to the base class
  Long64_t                   fNProcessed{0};         ///< Number of processed entries
  NBinning *                 fBiningSource{nullptr}; ///< Pointer to the source binning (from the original NGnTree)
  json                       fCfg{};                 ///< Configuration object
  bool                       fIsPureCopy{false};     ///< Flag indicating pure copy mode
  std::string                fResultsFilename{};     ///< Shared-FS path to copy result to after Close(true) (TCP mode)
  std::vector<Long64_t>      fCurrentDefinitionIds;  ///< Worker-local override for current definition id mapping
  std::unordered_set<Long64_t> fProcessedBinIds{};  //!< Set of already-processed global bin IDs (duplicate guard)
  std::vector<TObject *>     fDeferredDeletes;       //!< Objects deferred for single-threaded deletion

  /// \cond CLASSIMP
  ClassDef(NGnThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
