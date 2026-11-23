#ifndef Ndmspc_NBinning_H
#define Ndmspc_NBinning_H
#include <TObject.h>
#include <THnSparse.h>
#include <TAxis.h>
#include <chrono>
#include <cstddef>
#include <set>
#include <string>
#include <vector>
#include <map>
#include <TObjArray.h>
#include "NLogger.h"
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
 * @struct NBinningPointAxis
 * @brief One axis' selection in a prefilled point: which bins of that axis the point takes.
 *
 * A categorical axis is named by its bin `Label`; a numeric axis by a 1-based bin number
 * (`Bin`), an inclusive bin range (`Bins`, either end may be left open) or a value range in
 * axis units (`Range`). `Rebin` names one group of a binning definition (`{rebin, start, id}`),
 * the same vocabulary `NUtils` uses for bin specifications. `All()` is the whole axis, i.e. the
 * range with **both** ends open.
 *
 * Every form resolves to the same thing: the set of mapped entries of that axis whose base-bin
 * range overlaps the selection. A range is a range of **bins**, never of values — a value range
 * is the separate `Range` form.
 */
struct NBinningPointAxis {
  /// An open range end: no limit in that direction. Also what `All()` is made of.
  static constexpr int OPEN = 0;

  /// How a selection was written.
  enum class Form { kLabel, kBin, kRange, kValueRange, kRebin, kAll };

  Form        form{Form::kAll}; ///< The form as written
  std::string label;            ///< kLabel: the bin label
  int         minBin{OPEN};     ///< kBin/kRange: first bin (1-based), OPEN when unbounded
  int         maxBin{OPEN};     ///< kBin/kRange: last bin (1-based), OPEN when unbounded
  double      from{0};          ///< kValueRange: lower value in axis units
  double      to{0};            ///< kValueRange: upper value in axis units
  int         rebin{1};         ///< kRebin: rebin width
  int         start{1};         ///< kRebin: rebin start
  int         id{1};            ///< kRebin: the group id within that rebin

  /// An exact bin **label** (a categorical axis).
  static NBinningPointAxis Label(const std::string & value);
  /// One exact **bin**, 1-based.
  static NBinningPointAxis Bin(int bin);
  /// An inclusive **bin range**; either end may be `OPEN`.
  static NBinningPointAxis Bins(int first, int last);
  /// A **value range** in axis units, resolved to bins with `FindFixBin`.
  static NBinningPointAxis Range(double first, double last);
  /// One group of a binning definition.
  static NBinningPointAxis Rebin(int width, int start, int id);
  /// The whole axis — the range with both ends open.
  static NBinningPointAxis All() { return Bins(OPEN, OPEN); }

  /// Whether two selections name the same thing — compared by the bins they resolve to.
  bool operator==(const NBinningPointAxis & other) const
  {
    if (form != other.form) return false;
    switch (form) {
    case Form::kLabel: return label == other.label;
    case Form::kRebin: return rebin == other.rebin && start == other.start && id == other.id;
    case Form::kValueRange: return from == other.from && to == other.to;
    case Form::kBin:
    case Form::kRange:
    case Form::kAll: return minBin == other.minBin && maxBin == other.maxBin;
    }
    return false;
  }
  bool operator!=(const NBinningPointAxis & other) const { return !(*this == other); }
};

/**
 * @brief Read one axis' selection from its JSON form.
 *
 * Accepts the spellings a config can carry:
 *
 * | form | meaning |
 * | --- | --- |
 * | `"dca_0p05"` | a bin label; `"*"` and `"all"` mean the whole axis |
 * | `3` | one bin, 1-based |
 * | `[2, 5]` | an inclusive bin range |
 * | `[2]`, `[2, null]`, `[null, 5]`, `[]` | a bin range with an open end |
 * | `{"from": 0.5, "to": 1.5}` | a value range in axis units |
 * | `{"rebin": 2, "start": 1, "id": 3}` | one group of a binning definition |
 * | `{"label": "all"}` | the label `all`, when the whole axis is not meant |
 *
 * @param value The JSON value.
 * @param ok Set to false when the value is none of the above; the result is then `All()`.
 * @return The selection.
 */
NBinningPointAxis PointAxisFromJson(const json & value, bool * ok = nullptr);

/**
 * @brief Read a point list from its JSON form: an array of arrays of `PointAxisFromJson` values.
 * @param value The JSON value.
 * @return One entry per point; a point with an unreadable axis is dropped.
 */
std::vector<std::vector<NBinningPointAxis>> PointsFromJson(const json & value);

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
   *
   * Enumerates the full product of every axis' mapped entries. `FillPoints` with all-full-range
   * points does exactly the same, and is the form to use when the caller knows which points
   * exist.
   *
   * @param def Optional binning definition.
   * @return Number of filled bins.
   */
  Long64_t FillAll(NBinningDef * def = nullptr);

  /**
   * @brief Fill only the given points instead of the whole product of bins.
   *
   * A point is a selection per axis; the bins filled for it are that point's own product, and
   * points never multiply against each other. A full range — `All()`, or an axis the point does
   * not reach — takes every entry of that axis, so a point of all full ranges fills exactly what
   * `FillAll` fills. A bin reached by more than one point is filled once.
   *
   * This is what makes a sparse binning affordable: a definition whose axes describe 5.8M
   * combinations but whose data holds 23 points is filled in 23 × (the entries the points leave
   * open), not 5.8M.
   *
   * @param def    The definition to fill.
   * @param points One entry per point, one `NBinningPointAxis` per pinned axis.
   * @return Number of distinct bins filled.
   */
  Long64_t FillPoints(NBinningDef * def, const std::vector<std::vector<NBinningPointAxis>> & points);

  /**
   * @brief Resolve one point's per-axis selections to the indices of the mapped entries they cover.
   *
   * Each selection is matched against the base-bin range of the entries in that axis' content;
   * an entry is taken when its base range overlaps the selection. An axis the point does not
   * reach is taken whole, as `All()` would. Warns and skips a selection that matches nothing.
   *
   * @param point One `NBinningPointAxis` per pinned axis.
   * @param content The per-axis entries, as `CollectAxisContent` produces them. When null they are
   *                read from the current definition (or @p definitionName), rebuilding `fMap`,
   *                which a completed fill leaves cleared.
   * @param definitionName The definition to read when @p content is null; the current one by default.
   * @return For each axis, the 1-based indices into that axis' content; empty when nothing matched.
   */
  std::vector<std::vector<int>> ResolvePoint(const std::vector<NBinningPointAxis> & point,
                                            const std::vector<std::vector<std::vector<int>>> * content = nullptr,
                                            const std::string & definitionName = "");

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
   * @param setCurrent When true, mark the returned definition as the current one.
   * @return Pointer to NBinningDef.
   */
  NBinningDef * GetDefinition(const std::string & name = "", bool setCurrent = false);

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
   * @brief Add a binning definition, filled with the given points only.
   *
   * The binning map says how each axis is binned, exactly as the overload above; `points` says
   * which of those bins exist. A point pins the leading axes it names (a label, a bin, a bin
   * range, a value range or a rebin group) and leaves the rest open, so the definition is filled
   * without enumerating combinations the data does not have. Passing no points is `FillAll`.
   *
   * @param name Definition name.
   * @param binning Map of axis names to binning vectors.
   * @param points One entry per point, one `NBinningPointAxis` per pinned axis.
   * @param forceDefault If true, force as default definition.
   */
  void AddBinningDefinition(std::string name, std::map<std::string, std::vector<std::vector<int>>> binning,
                            const std::vector<std::vector<NBinningPointAxis>> & points, bool forceDefault = false);

  /**
   * @brief Add more points to a definition that already exists.
   *
   * Fills the definition again with the given points and appends what is new; a bin it already
   * holds is left alone, so appending the same point twice is a no-op and a point overlapping an
   * earlier one adds only its unseen bins. The definition's binning is unchanged, and ids keep
   * their order with new ones appended.
   *
   * The mapping is rebuilt from the definition's own binning (`fMap` is cleared once a fill
   * completes), so no state is kept beyond what the definition already carries.
   *
   * @param name The existing definition.
   * @param points One entry per point, as `AddBinningDefinition`'s third argument.
   * @return Number of **new** bins appended; 0 when the name is unknown or nothing was new.
   */
  Long64_t AppendPoints(const std::string & name, const std::vector<std::vector<NBinningPointAxis>> & points);

  /**
   * @brief Fill a definition over the whole product **in several executor runs**.
   *
   * `FillAll` is one executor over the entire product, which is fine until the product is large
   * enough that one run is too much to hold, too long to leave uninterruptible, or too big to
   * retry as a unit. This does the same work split into runs of at most @p pointsPerRun points
   * along the outermost axis: each run is its own `NDimensionalExecutor`, and the definition
   * keeps what the earlier runs filled, so a failure or a cancellation costs one chunk.
   *
   * This is what `AppendPoints` is for: a run that stops early can be resumed by appending the
   * points it did not reach, and the bins already held are skipped rather than repeated.
   *
   * @param name The definition to fill; it must exist and have no ids yet.
   * @param pointsPerRun Most points one executor run covers; 0 means a single run (`FillAll`).
   * @return Number of bins filled across all runs.
   */
  Long64_t FillInRuns(const std::string & name, int pointsPerRun = 0);

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

  /**
   * @brief Get the index of an axis by name.
   * @param name Axis name to search for.
   * @return Zero-based axis index, or -1 if the axis is not found.
   */
  int GetAxisIndex(const std::string & name) const;

  /**
   * @brief Set the internal content histogram.
   * @param content Pointer to the THnSparse histogram used as bin content storage.
   */
  void SetContent(THnSparse * content) { fContent = content; }

  private:
  /**
   * @brief The per-axis list of mapped entries, read off `fMap`.
   *
   * The cheap half of a fill: one `fMap` walk, one entry per mapped bin, and the axis types it
   * implies. Each entry is the stored `{dim, rebin, start, bin}` coordinates (or `{dim, bin}` for
   * a single/user axis), which is what a point selection is matched against.
   *
   * @param def The definition whose variable axes are marked. May be null.
   * @return For each axis, its entries in map order.
   */
  std::vector<std::vector<std::vector<int>>> CollectAxisContent(NBinningDef * def);

  /**
   * @brief Fill one already-resolved point: the product of its per-axis entry indices.
   *
   * The body `FillAll` and `FillPoints` share. Bins in @p seen are skipped, which is what keeps
   * overlapping points from filling a bin twice.
   *
   * @param content The per-axis entries from `CollectAxisContent`.
   * @param resolved For each axis, the 1-based indices into `content` this point takes.
   * @param seen Content bins already filled; updated.
   * @param nBinsFilled Running count of bins filled; updated.
   * @param nTotalBins Progress denominator.
   * @param start Progress start time.
   * @param runOutput Whether to draw a progress bar.
   * @param def The definition filled.
   */
  void FillResolvedPoint(const std::vector<std::vector<std::vector<int>>> & content,
                         const std::vector<std::vector<int>> & resolved, std::set<Long64_t> & seen,
                         Long64_t & nBinsFilled, Long64_t nTotalBins,
                         const std::chrono::high_resolution_clock::time_point & start, bool runOutput,
                         NBinningDef * def);

  /**
   * @brief Apply a definition's per-axis binning to `fMap`.
   *
   * The body `AddBinningDefinition` and `AppendPoints` share: `fMap` is cleared once a fill
   * completes, so appending re-applies the definition's own binning rather than keeping state
   * beside it.
   *
   * @param name Definition name, for messages.
   * @param binning The definition's per-axis binning.
   * @return True when every axis could be applied.
   */
  bool ApplyDefinitionBinning(const std::string & name,
                              const std::map<std::string, std::vector<std::vector<int>>> & binning);

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
