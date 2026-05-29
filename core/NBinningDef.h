#ifndef Ndmspc_NBinningDef_H
#define Ndmspc_NBinningDef_H
#include <string>
#include <vector>
#include <map>
#include <TObject.h>
#include <TAxis.h>
#include <THnSparse.h>
namespace Ndmspc {

/**
 * @class NBinningDef
 * @brief Defines binning mapping and content for NDMSPC histograms.
 *
 * Stores axis definitions, variable axes, bin IDs, and template histogram content.
 * Provides methods for refreshing IDs/content, axis management, and integration with NBinning.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NBinning;

/**
 * @class NBinningDef
 * @brief NBinningDef object for binning mapping and content definition.
 */
class NBinningDef : public TObject {
  public:
  /**
   * @brief Constructor.
   * @param name Name of the binning definition.
   * @param definition Mapping of axis names to binning vectors.
   * @param binning Pointer to parent NBinning object.
   */
  NBinningDef(std::string name = "default", std::map<std::string, std::vector<std::vector<int>>> definition = {},
              NBinning * binning = nullptr);

  /**
   * @brief Destructor.
   */
  virtual ~NBinningDef();

  /**
   * @brief Print binning definition information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const;

  /**
   * @brief Get the binning mapping definition.
   * @return Mapping of axis names to binning vectors.
   */
  std::map<std::string, std::vector<std::vector<int>>> GetDefinition() const { return fDefinition; }

  /**
   * @brief Set the binning mapping definition.
   * @param d Mapping of axis names to binning vectors.
   */
  void SetDefinition(const std::map<std::string, std::vector<std::vector<int>>> & d) { fDefinition = d; }

  /**
   * @brief Set axis definition for a specific axis.
   * @param axisName Name of the axis.
   * @param d Binning vectors for the axis.
   */
  void SetAxisDefinition(std::string axisName, const std::vector<std::vector<int>> & d) { fDefinition[axisName] = d; }

  /**
   * @brief Get the cached bin edges for a named axis.
   * @param axisName Name of the axis to query.
   * @return A vector of integer bin edge indices for the given axis.
   * @throws std::out_of_range if the axis name is not present in the cache.
   */
  std::vector<int> GetBins(std::string axisName) const { return fBins.at(axisName); }

  /**
   * @brief Determine whether a coordinate tuple corresponds to a defined bin.
   *
   * The `coords` pointer is expected to point to an array of integer
   * coordinates (one per axis) matching the axes in the `fContent`
   * THnSparse global mapping. The method checks whether these coordinates map
   * to a bin that exists in this binning definition.
   *
   * @param coords Pointer to array of bin indices (one per axis).
   * @return `true` if the coordinates map to a defined bin, `false` otherwise.
   */
  bool ContainsBin(Int_t* coords) const;

  /**
   * @brief Get list of variable axes indices.
   * @return Vector of variable axis indices.
   */
  std::vector<int> GetVariableAxes() const { return fVariableAxes; }

  /**
   * @brief Add a variable axis index.
   * @param axis Axis index to add.
   */
  void AddVariableAxis(size_t axis) { fVariableAxes.push_back(axis); }

  // /**
  //  * @brief Refresh content histogram from bin IDs.
  //  */
  // void RefreshContentFromIds();

  // /**
  //  * @brief Refresh bin IDs from content histogram.
  //  */
  // void RefreshIdsFromContent();

  /**
   * @brief Get list of bin IDs.
   * @return Vector of bin IDs.
   */
  std::vector<Long64_t> GetIds() const { return fIds; }

  /**
   * @brief Get reference to list of bin IDs.
   * @return Reference to vector of bin IDs.
   */
  std::vector<Long64_t> & GetIds() { return fIds; }

  /**
   * @brief Get bin ID at specified index.
   * @param index Index of bin.
   * @return Bin ID.
   */
  Long64_t GetId(size_t index) const;

  /**
   * @brief Get list of axes from content histogram.
   * @return Pointer to TObjArray of axes.
   */
  TObjArray * GetAxes() const { return fContent->GetListOfAxes(); }

  /**
   * @brief Get the template content histogram.
   * @return Pointer to THnSparse histogram.
   */
  THnSparse * GetContent() const { return fContent; }

  /**
   * @brief Get pointer to parent NBinning object.
   * @return Pointer to NBinning.
   */
  NBinning * GetBinning() const { return fBinning; }

  /**
   * @brief Get the name of this binning definition.
   *
   * Overrides TObject::GetName() to return the stored std::string name.
   * @return C-string pointer to the name.
   */
  virtual const char* GetName() const { return fName.c_str(); }




  private:
  NBinning *                                           fBinning{nullptr}; ///< Pointer to the parent binning
  std::string                                          fName;             ///< Name of the binning definition
  std::map<std::string, std::vector<std::vector<int>>> fDefinition;       ///< Binning mapping definition
  std::map<std::string, std::vector<int>> fBins;       ///< Cached bin edges for each axis name
  std::vector<int>      fVariableAxes{};   ///< List of variable axes indices in the content histogram
  std::vector<Long64_t> fIds{};            ///< List of IDs for the binning definition
  THnSparse *           fContent{nullptr}; ///< Template histogram for the binning definition

  /// \cond CLASSIMP
  ClassDef(NBinningDef, 2);
  /// \endcond;
};
} // namespace Ndmspc
#endif
