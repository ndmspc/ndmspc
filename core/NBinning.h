#ifndef Ndmspc_NBinning_H
#define Ndmspc_NBinning_H
#include <TObject.h>
#include <THnSparse.h>
#include <TAxis.h>
#include <cstddef>
#include <vector>
#include <map>
#include "TObjArray.h"
#include "NBinningDef.h"
#include "NBinningPoint.h"

namespace Ndmspc {

/**
 * @enum Binning
 * @brief Types of binning supported.
 */
enum class Binning {
  kSingle    = 0, ///< No rebin possible
  kMultiple  = 1, ///< Rebin is possible
  kUser      = 2, ///< No rebin possible, but axis range is as single bin (User must set bin as needed)
  kUndefined = 3  ///< Undefined binning type
};

/**
 * @enum AxisType
 * @brief Types of axis supported.
 */
enum class AxisType {
  kFixed     = 0, ///< fixed axis type
  kVariable  = 1, ///< variable axis type
  kUndefined = 2, ///< Undefined axis type
};

/**
 * @class NBinning
 * @brief NBinning object for managing multi-dimensional binning and axis definitions.
 *
 * Provides methods for axis management, binning definitions, mapping, and content handling.
 * Supports fixed and variable binning, user-defined axes, and integration with THnSparse histograms.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NBinning : public TObject {
  public:
  /**
   * @brief Default constructor.
   */
  NBinning();

  /**
   * @brief Constructor from TObjArray of axes.
   * @param axes Array of axis objects.
   */
  NBinning(TObjArray * axes);

  /**
   * @brief Constructor from vector of TAxis pointers.
   * @param axes Vector of axis pointers.
   */
  NBinning(std::vector<TAxis *> axes);

  /**
   * @brief Destructor.
   */
  virtual ~NBinning();

  /**
   * @brief Initialize the binning object.
   */
  void Initialize();

  /**
   * @brief Reset the binning object to initial state.
   */
  void Reset();

  /**
   * @brief Print binning information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const;

  /**
   * @brief Print binning content.
   * @param option Print options.
   */
  void PrintContent(Option_t * option = "") const;

  /**
   * @brief Fill all bins according to definition.
   * @param def Optional binning definition.
   * @return Number of filled bins.
   */
  Long64_t FillAll(NBinningDef * def = nullptr);

  /**
   * @brief Get coordinate ranges for given coordinates.
   * @param c Vector of coordinates.
   * @return Vector of coordinate ranges.
   */
  std::vector<std::vector<int>> GetCoordsRange(std::vector<int> c) const;

  /**
   * @brief Get axis ranges for given coordinates.
   * @param c Vector of coordinates.
   * @return Vector of axis ranges.
   */
  std::vector<std::vector<int>> GetAxisRanges(std::vector<int> c) const;

  /**
   * @brief Get axis range for a specific axis and coordinates.
   * @param axisId Axis index.
   * @param min Output minimum value.
   * @param max Output maximum value.
   * @param c Vector of coordinates.
   * @return True if range found.
   */
  bool GetAxisRange(size_t axisId, double & min, double & max, std::vector<int> c) const;

  /**
   * @brief Get axis range in base for a specific axis and coordinates.
   * @param axisId Axis index.
   * @param min Output minimum bin.
   * @param max Output maximum bin.
   * @param c Vector of coordinates.
   * @return True if range found.
   */
  bool GetAxisRangeInBase(size_t axisId, int & min, int & max, std::vector<int> c) const;

  /**
   * @brief Generate a list of axes as TObjArray.
   * @return Pointer to TObjArray of axes.
   */
  TObjArray * GenerateListOfAxes();

  /**
   * @brief Get binning for a specific axis and coordinates.
   * @param axisId Axis index.
   * @param c Vector of coordinates.
   * @return Vector of binning values.
   */
  std::vector<int> GetAxisBinning(size_t axisId, const std::vector<int> & c) const;

  /**
   * @brief Get indexes of axes by type.
   * @param type AxisType to filter.
   * @return Vector of axis indexes.
   */
  std::vector<int> GetAxisIndexes(AxisType type) const;

  /**
   * @brief Get axis indexes by axis names.
   * @param axisNames Vector of axis names.
   * @return Vector of axis indexes.
   */
  std::vector<int> GetAxisIndexesByNames(std::vector<std::string> axisNames) const;

  /**
   * @brief Get axis names by type.
   * @param type AxisType to filter.
   * @return Vector of axis names.
   */
  std::vector<std::string> GetAxisNamesByType(AxisType type) const;

  /**
   * @brief Get axis names by indexes.
   * @param axisIds Vector of axis indexes.
   * @return Vector of axis names.
   */
  std::vector<std::string> GetAxisNamesByIndexes(std::vector<int> axisIds) const;

  /**
   * @brief Add binning for a specific axis.
   * @param id Axis index.
   * @param binning Vector of binning values.
   * @param n Optional number of bins.
   * @return True if added successfully.
   */
  bool AddBinning(size_t id, std::vector<int> binning, size_t n = 1);

  /**
   * @brief Add variable binning for a specific axis.
   * @param id Axis index.
   * @param mins Vector of minimum values.
   * @return True if added successfully.
   */
  bool AddBinningVariable(size_t id, std::vector<int> mins);

  /**
   * @brief Add binning via bin widths for a specific axis.
   * @param id Axis index.
   * @param widths Vector of bin widths.
   * @return True if added successfully.
   */
  bool AddBinningViaBinWidths(size_t id, std::vector<std::vector<int>> widths);

  /**
   * @brief Set axis type for a specific axis.
   * @param id Axis index.
   * @param type AxisType to set.
   * @return True if set successfully.
   */
  bool SetAxisType(size_t id, AxisType type);

  /**
   * @brief Get the mapping histogram.
   * @return Pointer to THnSparse map histogram.
   */
  THnSparse * GetMap() const { return fMap; }

  /**
   * @brief Get the content histogram.
   * @return Pointer to THnSparse content histogram.
   */
  THnSparse * GetContent() const { return fContent; }

  /**
   * @brief Get vector of axis pointers.
   * @return Vector of TAxis pointers.
   */
  std::vector<TAxis *> GetAxes() const { return fAxes; }

  /**
   * @brief Get axes by type.
   * @param type AxisType to filter.
   * @return Vector of TAxis pointers.
   */
  std::vector<TAxis *> GetAxesByType(AxisType type) const;

  /**
   * @brief Get binning type for a specific axis.
   * @param i Axis index.
   * @return Binning type.
   */
  Binning GetBinningType(size_t i) const;

  /**
   * @brief Get axis type for a specific axis.
   * @param i Axis index.
   * @return AxisType.
   */
  AxisType GetAxisType(size_t i) const;

  /**
   * @brief Get axis type as character for a specific axis.
   * @param i Axis index.
   * @return Character representing axis type.
   */
  char GetAxisTypeChar(size_t i) const;

  /**
   * @brief Get binning definition by name.
   * @param name Definition name (optional).
   * @return Pointer to NBinningDef.
   */
  NBinningDef * GetDefinition(const std::string & name = "");

  /**
   * @brief Get all binning definitions.
   * @return Map of definition names to NBinningDef pointers.
   */
  std::map<std::string, NBinningDef *> GetDefinitions() const { return fDefinitions; }

  /**
   * @brief Get all definition names.
   * @return Vector of definition names.
   */
  std::vector<std::string> GetDefinitionNames() const { return fDefinitionNames; }

  /**
   * @brief Get current definition name.
   * @return Current definition name string.
   */
  std::string GetCurrentDefinitionName() const { return fCurrentDefinitionName; }

  /**
   * @brief Set current definition name.
   * @param name Definition name to set.
   */
  void SetCurrentDefinitionName(const std::string & name);

  /**
   * @brief Add a binning definition.
   * @param name Definition name.
   * @param binning Map of axis names to binning vectors.
   * @param forceDefault If true, force as default definition.
   */
  void AddBinningDefinition(std::string name, std::map<std::string, std::vector<std::vector<int>>> binning,
                            bool forceDefault = false);

  /**
   * @brief Get the current binning point.
   * @return Pointer to NBinningPoint.
   */
  NBinningPoint * GetPoint();

  /**
   * @brief Get binning point for a specific axis and binning.
   * @param id Axis index.
   * @param binning Binning name.
   * @return Pointer to NBinningPoint.
   */
  NBinningPoint * GetPoint(size_t id, const std::string binning = "");

  /**
   * @brief Set the current binning point.
   * @param point Pointer to NBinningPoint.
   */
  void SetPoint(NBinningPoint * point) { fPoint = point; }

  /**
   * @brief Set configuration from JSON.
   * @param cfg JSON configuration.
   * @return True if configuration set successfully.
   */
  bool SetCfg(const json & cfg);

  private:
  THnSparse *                          fMap{nullptr};              ///< Mapping histogram
  THnSparse *                          fContent{nullptr};          ///< Content histogram
  std::vector<TAxis *>                 fAxes;                      ///< List of axes
  std::vector<Binning>                 fBinningTypes;              ///< Binning types
  std::vector<AxisType>                fAxisTypes;                 ///< Axis types
  std::string                          fCurrentDefinitionName{""}; ///< Current definition name
  std::map<std::string, NBinningDef *> fDefinitions;               ///< Binning definitions
  std::vector<std::string>             fDefinitionNames;           ///< Binning definition names
  NBinningPoint *                      fPoint{nullptr};            ///<! Binning point object

  /// \cond CLASSIMP
  ClassDef(NBinning, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
