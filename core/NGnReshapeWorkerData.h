#ifndef Ndmspc_NGnReshapeWorkerData_H
#define Ndmspc_NGnReshapeWorkerData_H

#include <map>
#include <string>
#include <vector>

#include "NGnNavigator.h"
#include "NGnTree.h"
#include "NThreadData.h"

namespace Ndmspc {

/**
 * @class NGnReshapeWorkerData
 * @brief Thread-local worker for a parallel reshape of the first navigator layer.
 *
 * A reshape of the first layer is one independent recursion per bin of that layer: each filled bin maps
 * to its own projection cell and builds its own subtree, so the bins can be reshaped concurrently as
 * long as every worker reads the tree and the binning content through objects of its own.
 *
 * That is what this class provides, mirroring `NGnThreadData` on the `Process` path: `Init` opens a
 * private `NGnTree` from the same file (private `TTree` buffers, private `NBinning` and therefore a
 * private `NBinningDef::GetContent()` sparse), and `Process` reshapes one bin through
 * `NGnNavigator::ReshapeBins`. The subtrees are collected by projection index and handed to the real
 * navigator afterwards, so no merge step is needed: the write targets are disjoint, one bin per cell.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NGnReshapeWorkerData : public NThreadData {
  public:
  /**
   * @brief Constructor.
   */
  NGnReshapeWorkerData();

  /**
   * @brief Destructor. Closes the private tree.
   */
  virtual ~NGnReshapeWorkerData();

  /**
   * @brief Open this worker's private tree and record what to reshape.
   *
   * @param id Worker index (names the thread in the logs).
   * @param source Tree to reshape; the worker opens its own copy from the same file and branches.
   * @param binningName Binning definition to reshape in the private tree ("" = current definition).
   * @param levels Levels as passed to Reshape.
   * @param ranges Axis ranges for the first layer.
   * @param rangesBase Base-axis ranges for the first layer.
   * @return True when the private tree was opened and the binning resolved.
   */
  bool Init(size_t id, NGnTree * source, const std::string & binningName, std::vector<std::vector<int>> levels,
            std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase);

  /**
   * @brief Reshape one bin of the first layer (virtual from NThreadData).
   * @param coords Projection coordinates of the bin, one per axis of the first layer.
   */
  virtual void Process(const std::vector<int> & coords) override;

  /**
   * @brief Record where the bin this worker reshapes sits in its layer's progress ("3/5").
   *
   * The layer's own bar is drawn by the caller, which alone knows the total; without this the worker's
   * progress tag could name its level but not its place in it.
   *
   * @param place "done/total" for the layer, or empty to leave it out.
   */
  void SetProgressOfLevel(const std::string & place) { fProgressOfLevel = place; }

  /**
   * @brief The subtrees this worker built, keyed by the projection cell they belong at.
   *
   * The caller owns them from here on: it attaches each one to the real navigator's slot and deletes
   * whatever it does not attach. Calling this hands the map over and leaves the worker empty.
   *
   * @return Subtree per projection index.
   */
  std::map<int, NGnNavigator *> TakeSubtrees();

  /**
   * @brief Number of bins this worker reshaped.
   * @return Processed bin count.
   */
  size_t GetNReshaped() const { return fNReshaped; }

  private:
  /**
   * @brief Open this worker's private tree, on the thread that will read it.
   * @return True when the tree was opened and the binning resolved.
   */
  bool OpenTree();

  NGnTree *                      fTree{nullptr};    ///< Private tree (owned)
  std::string                    fFileName{};       ///< File to open
  std::string                    fTreeName{};       ///< Tree to open
  std::string                    fBinningName{};    ///< Definition to reshape
  std::vector<std::vector<int>>  fLevels{};         ///< Levels to reshape
  std::map<int, std::vector<int>> fRanges{};        ///< Ranges for the first layer
  std::map<int, std::vector<int>> fRangesBase{};    ///< Base ranges for the first layer
  size_t                         fNReshaped{0};     ///< Bins reshaped
  std::string                    fProgressOfLevel{}; ///< Where this worker's bin sits ("3/5")
  std::map<int, NGnNavigator *>  fSubtrees{};       ///< Subtree per projection index (owned)

  /// \cond CLASSIMP
  ClassDefOverride(NGnReshapeWorkerData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
