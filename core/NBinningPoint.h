#ifndef Ndmspc_NBinningPoint_H
#define Ndmspc_NBinningPoint_H
#include <TObject.h>
#include <nlohmann/json.hpp>
#include "RtypesCore.h"
using json = nlohmann::json;

namespace Ndmspc {

/**
 * @class NBinningPoint
 * @brief Represents a single point in multi-dimensional binning.
 *
 * Stores coordinates, axis ranges, labels, and configuration for a binning point.
 * Provides methods for coordinate management, storage mapping, content filling, and integration with NBinning.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NBinning;
class NStorageTree;
class NGnTree;
class NBinningPoint : public TObject {
  public:
  /**
   * @brief Constructor.
   * @param b Pointer to NBinning object.
   */
  NBinningPoint(NBinning * b = nullptr);

  /**
   * @brief Destructor.
   */
  virtual ~NBinningPoint();

  /**
   * @brief Print binning point information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const;

  /**
   * @brief Reset the binning point to initial state.
   */
  virtual void Reset();

  /**
   * @brief Get number of dimensions in content histogram.
   * @return Number of dimensions.
   */
  Int_t GetNDimensionsContent() const { return fContentNDimensions; }

  /**
   * @brief Get pointer to content coordinates array.
   * @return Pointer to coordinates array.
   */
  Int_t * GetCoords() const { return fContentCoords; }

  /**
   * @brief Get number of dimensions.
   * @return Number of dimensions.
   */
  Int_t GetNDimensions() const { return fNDimensions; }

  /**
   * @brief Get pointer to storage coordinates array.
   * @return Pointer to storage coordinates array.
   */
  Int_t * GetStorageCoords() const { return fStorageCoords; }

  /**
   * @brief Recalculate storage coordinates for the point.
   * @param entry Entry number (optional).
   * @param useBinningDefCheck Use binning definition check.
   * @return True if recalculation successful.
   */
  bool RecalculateStorageCoords(Long64_t entry = -1, bool useBinningDefCheck = false);

  /**
   * @brief Set point content from linear index.
   * @param linBin Linear bin index.
   * @param checkBinningDef Check binning definition.
   * @return True if set successfully.
   */
  bool SetPointContentFromLinearIndex(Long64_t linBin, bool checkBinningDef = false);

  /**
   * @brief Fill the binning point content.
   * @param ignoreFilledCheck Ignore filled check.
   * @return Number of filled bins.
   */
  Long64_t Fill(bool ignoreFilledCheck = false);

  /**
   * @brief Get reference to configuration JSON object.
   * @return Reference to JSON configuration.
   */
  json & GetCfg() { return fCfg; }

  /**
   * @brief Set configuration JSON object.
   * @param cfg JSON configuration.
   */
  void SetCfg(const json & cfg) { fCfg = cfg; }

  /**
   * @brief Get base axis ranges for the point.
   * @return Map of axis index to vector of ranges.
   */
  std::map<int, std::vector<int>> GetBaseAxisRanges() const;

  /**
   * @brief Returns a string representation of the binning point.
   *
   * @param prefix Optional prefix to prepend to the string.
   * @param all If true, includes all details in the string; otherwise, provides a summary.
   * @return std::string String representation of the binning point.
   */
  std::string GetString(const std::string & prefix = "", bool all = false) const;

  /**
   * @brief Get labels for each axis.
   * @return Vector of axis labels.
   */
  std::vector<std::string> GetLabels() const { return fLabels; }

  /**
   * @brief Get the array of minimum values for all axes.
   * @return Pointer to the array of minimum values.
   */
  Double_t * GetMins() const { return fMins; }

  /**
   * @brief Get the array of maximum values for all axes.
   * @return Pointer to the array of maximum values.
   */
  Double_t * GetMaxs() const { return fMaxs; }

  /**
   * @brief Get the minimum value for a specific axis.
   * @param axis The name of the axis.
   * @return The minimum value for the given axis.
   */
  Double_t GetMin(std::string axis) const;

  /**
   * @brief Get the maximum value for a specific axis.
   * @param axis The name of the axis.
   * @return The maximum value for the given axis.
   */
  Double_t GetMax(std::string axis) const;

  /**
   * @brief Get the label for a specific axis.
   * @param axis The name of the axis.
   * @return The label for the given axis.
   */
  std::string GetLabel(std::string axis) const;

  /**
   * @brief Get pointer to NBinning object.
   * @return Pointer to NBinning.
   */
  NBinning * GetBinning() const { return fBinning; }

  /**
   * @brief Set NBinning object pointer.
   * @param b Pointer to NBinning.
   */
  void SetBinning(NBinning * b);

  /**
   * @brief Get pointer to storage tree object.
   * @return Pointer to NStorageTree.
   */
  NStorageTree * GetTreeStorage() const { return fTreeStorage; }

  /**
   * @brief Set storage tree object pointer.
   * @param s Pointer to NStorageTree.
   */
  void SetTreeStorage(NStorageTree * s) { fTreeStorage = s; }

  /**
   * @brief Get pointer to input NGnTree object.
   * @return Pointer to NGnTree.
   */
  NGnTree * GetInput() const { return fInput; }

  /**
   * @brief Set input NGnTree object pointer.
   * @param input Pointer to NGnTree.
   */
  void SetInput(NGnTree * input) { fInput = input; }

  /**
   * @brief Set entry number for the point.
   * @param entry Entry number.
   */
  void SetEntryNumber(Long64_t entry) { fEntryNumber = entry; }

  /**
   * @brief Get entry number for the point.
   * @return Entry number.
   */
  Long64_t GetEntryNumber() const { return fEntryNumber; }

  private:
  json                     fCfg{};                  ///< Configuration object
  NGnTree *                fInput{nullptr};         ///< Input NGnTree object
  NBinning *               fBinning{nullptr};       ///< Binning object
  NStorageTree *           fTreeStorage{nullptr};   ///< Storage tree object
  Int_t                    fContentNDimensions{1};  ///< Number of dimensions in content histogram
  Int_t *                  fContentCoords{nullptr}; ///< Coordinates of the point
  Int_t                    fNDimensions{1};         ///< Number of dimensions
  Int_t *                  fStorageCoords{nullptr}; ///< Storage coordinates of the point
  Double_t *               fMins{nullptr};          ///< Minimum values for each axis
  Double_t *               fMaxs{nullptr};          ///< Maximum values for each axis
  Int_t *                  fBaseBinMin{nullptr};    ///< Base bin minimum (for variable binning)
  Int_t *                  fBaseBinMax{nullptr};    ///< Base bin maximum (for variable binning)
  std::vector<std::string> fLabels{};               ///< Labels for each axis
  Long64_t                 fEntryNumber{-1};        ///< Entry in the storage tree

  /// \cond CLASSIMP
  ClassDef(NBinningPoint, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
