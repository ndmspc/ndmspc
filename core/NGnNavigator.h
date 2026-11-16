#ifndef Ndmspc_NGnNavigator_H
#define Ndmspc_NGnNavigator_H
#include <TNamed.h>
#include <Buttons.h>
#include <TList.h>
#include <cstddef>
#include <vector>
#include "NBinningDef.h"
#include "NGnTree.h"
namespace Ndmspc {

/**
 * @class NGnNavigator
 * @brief Navigator object for managing hierarchical data structures and projections.
 *
 * Handles navigation, reshaping, exporting, drawing, and management of objects and parameters
 * in a hierarchical tree structure. Supports projections, children/parent relationships,
 * and integration with NBinningDef and NGnTree.
 *
 * ## What Reshape builds
 *
 * `levels` groups the variable axes of a binning definition into layers; `{{0}, {1, 2}}` is a
 * two-layer navigator, one axis on top and two below it. `Reshape` walks the layers in order and, for
 * each bin of a layer, projects that layer's axes into one histogram (the node's *projection*) and
 * creates one child per projection cell. A child that has layers below it is reshaped in turn, so the
 * result is a tree whose depth is `levels.size()` and whose branching follows the data: a bin with no
 * entries gets no child.
 *
 * Only the **last** layer fills analysis payload: the objects of the branches (`outputPoint` and any
 * other `TList` branch) and the parameters of `_params`. Upper layers carry structure and the
 * aggregated ranges their children report.
 *
 * ## Cost and parallelism
 *
 * The projection of a layer does not depend on the bin, so it is built once per layer and read by all
 * of its bins. Layers are reshaped one at a time. The first layer of a multi-layer reshape is split
 * across threads when the ROOT thread pool has room for them, since its bins are independent
 * recursions; `NDMSPC_NAV_WORKERS` overrides the count and `1` forces the serial path. A single-layer
 * reshape and the fork-based execution modes always run serially — see `ReshapeWorkerCount` for why.
 *
 * Long reshapes report per-layer progress through `NUtils::ProgressBar` when run output is on
 * (`NDMSPC_LOG_RUN=1`); `NDMSPC_RESHAPE_PROJECTION=per-bin` restores the older per-bin projection.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NGnNavigator : public TNamed {
  public:
  /**
   * @brief Constructor.
   * @param name Navigator name.
   * @param title Navigator title.
   * @param objectTypes Types of objects managed (default: {"TH1"}).
   */
  NGnNavigator(const char * name = "GnNavigator", const char * title = "Gn Navigator",
               std::vector<std::string> objectTypes = {"TH1"});

  /**
   * @brief Destructor.
   */
  virtual ~NGnNavigator();

  /**
   * @brief Reshape navigator using binning name and levels.
   *
   * The entry point: resolves the binning definition and validates `levels` before delegating to the
   * recursive form. Every variable axis of the definition must appear in `levels` exactly once; an axis
   * missing, repeated, or in a layer of its own left empty is an error and returns nullptr. With an empty
   * `levels` the definition's variable axes are taken as a single layer, which only works up to three
   * axes — a projection of four or more is refused.
   *
   * @param binningName Name of binning definition (empty = the current one).
   * @param levels Layers of axis indices, outermost first, e.g. `{{0}, {1, 2}}`.
   * @param level Current level (default: 0).
   * @param ranges Map of ranges for axes.
   * @param rangesBase Map of base ranges for axes.
   * @return Pointer to the reshaped root navigator, or nullptr when refused or cancelled.
   */
  NGnNavigator * Reshape(std::string binningName, std::vector<std::vector<int>> levels, size_t level = 0,
                         std::map<int, std::vector<int>> ranges = {}, std::map<int, std::vector<int>> rangesBase = {});

  /**
   * @brief Reshape navigator using NBinningDef and levels.
   * @param binningDef Pointer to NBinningDef.
   * @param levels Vector of levels.
   * @param level Current level (default: 0).
   * @param ranges Map of ranges for axes.
   * @param rangesBase Map of base ranges for axes.
   * @param parent Pointer to parent NGnNavigator.
   * @return Pointer to reshaped NGnNavigator.
   */
  NGnNavigator * Reshape(NBinningDef * binningDef, std::vector<std::vector<int>> levels, size_t level = 0,
                         std::map<int, std::vector<int>> ranges = {}, std::map<int, std::vector<int>> rangesBase = {},
                         NGnNavigator * parent = nullptr);

  /**
   * @brief Reshape a bin sub-range of one level, into the given parent.
   *
   * `Reshape` reshapes every bin of a level. This is the same recursion with its first level restricted
   * to `[minBins, maxBins]` (one entry per axis of that level, ROOT bins), which is what lets a reshape
   * of the first layer be split across workers: each one reshapes its own sub-range under its own root
   * navigator and its own `NGnTree`, and the resulting subtrees are attached afterwards.
   *
   * The restriction applies to **this** level only; deeper levels always span their full range. Empty
   * bounds mean "the whole level", i.e. exactly what `Reshape` does.
   *
   * @param binningDef Binning definition to reshape.
   * @param levels Levels as passed to Reshape.
   * @param level Level to reshape (0 when called for a worker).
   * @param ranges Axis ranges for this level.
   * @param rangesBase Base-axis ranges for this level.
   * @param parent Navigator to reshape into; null creates the root, as in Reshape.
   * @param minBins Per-axis first bin to reshape (empty = the level's first bin).
   * @param maxBins Per-axis last bin to reshape (empty = the level's last bin).
   * @return The reshaped navigator, or nullptr on failure.
   */
  NGnNavigator * ReshapeBins(NBinningDef * binningDef, std::vector<std::vector<int>> levels, size_t level,
                             std::map<int, std::vector<int>> ranges, std::map<int, std::vector<int>> rangesBase,
                             NGnNavigator * parent, const std::vector<int> & minBins,
                             const std::vector<int> & maxBins);

  /**
   * @brief Export navigator data to file.
   * @param filename Output file name.
   * @param objectNames Names of objects to export.
   * @param cfg Optional JSON configuration controlling the export (default: empty).
   */
  void Export(const std::string & filename, std::vector<std::string> objectNames, const std::string & cfg = "");

  /**
   * @brief Export navigator data to JSON.
   * @param j JSON object to fill.
   * @param obj Navigator object to export.
   * @param objectNames Names of objects to export.
   * @param cfg Optional JSON configuration controlling the export (default: empty).
   */
  void ExportToJson(json & j, NGnNavigator * obj, std::vector<std::string> objectNames, json cfg = {});

  /**
   * @brief Print navigator information.
   * @param option Print options.
   */
  virtual void Print(Option_t * option = "") const override;

  /**
   * @brief Draw navigator objects.
   * @param option Draw options.
   */
  virtual void Draw(Option_t * option = "") override;

  /**
   * @brief Retrieves information about the navigator in JSON format.
   *
   * @return json An object containing navigator information.
   */
  json GetInfoJson() const;

  /**
   * @brief Draw spectra for a parameter.
   * @param parameterName Name of parameter.
   * @param projIds Projection IDs.
   * @param minmax Min/max or percent range.
   * @param minmaxMode Mode for min/max calculation ("VE" = value±error, "V" = value only).
   * @param option Draw options.
   */
  virtual TList * DrawSpectra(std::string parameterName, std::vector<int> projIds, std::vector<double> minmax = {0.05},
                              const std::string& minmaxMode = "V", Option_t * option = "") const;

  /**
   * @brief Draws spectra for the given parameter and projection axes.
   * @param parameterName The name of the parameter to draw spectra for.
   * @param projAxes A vector of axis names to project onto.
   * @param minmax Min/max or percent range.
   * @param minmaxMode Mode for min/max calculation ("VE" = value±error, "V" = value only).
   * @param option Optional drawing options.
   */
  virtual TList * DrawSpectraByName(std::string parameterName, std::vector<std::string> projAxes,
                                    std::vector<double> minmax = {0.05}, const std::string& minmaxMode = "V", Option_t * option = "") const;

  /**
   * @brief Draws all spectra for the given parameter.
   * @param parameterName The name of the parameter to draw all spectra for.
   * @param minmax Min/max or percent range.
   * @param minmaxMode Mode for min/max calculation ("VE" = value±error, "V" = value only).
   * @param option Optional drawing options.
   */
  virtual TList * DrawSpectraAll(std::string parameterName, std::vector<double> minmax = {0.05},
                                 const std::string& minmaxMode = "V", Option_t * option = "") const;

  /**
   * @brief Paint navigator objects.
   * @param option Paint options.
   */
  virtual void Paint(Option_t * option = "") override;

  /**
   * @brief Calculate distance to primitive for graphical selection.
   * @param px X coordinate.
   * @param py Y coordinate.
   * @return Distance value.
   */
  Int_t DistancetoPrimitive(Int_t px, Int_t py) override;

  /**
   * @brief Handle execution of events (e.g., mouse, keyboard).
   * @param event Event type.
   * @param px X coordinate.
   * @param py Y coordinate.
   */
  virtual void ExecuteEvent(Int_t event, Int_t px, Int_t py) override;

  /**
   * @brief Get pointer to NGnTree object.
   * @return Pointer to NGnTree.
   */
  NGnTree * GetGnTree() const { return fGnTree; }

  /**
   * @brief Set NGnTree object pointer.
   * @param tree Pointer to NGnTree.
   */
  void SetGnTree(NGnTree * tree) { fGnTree = tree; }

  /**
   * @brief Point this navigator and all its descendants at the given tree.
   *
   * A subtree built on a worker points at the worker's private tree, which is closed when the worker is
   * done. Once such a subtree is attached to the real navigator, it has to be re-pointed at the real tree.
   *
   * @param tree Tree to point at.
   */
  void SetGnTreeRecursive(NGnTree * tree);

  /**
   * @brief Get vector of child navigators.
   * @return Vector of NGnNavigator pointers.
   */
  std::vector<NGnNavigator *> GetChildren() const { return fChildren; }

  /**
   * @brief Set number of children.
   * @param n Number of children.
   */
  void SetChildrenSize(size_t n) { fChildren.resize(n); }

  /**
   * @brief Get child navigator at index.
   * @param index Child index.
   * @return Pointer to child NGnNavigator.
   */
  NGnNavigator * GetChild(size_t index) const;

  /**
   * @brief Get the navigator at a drill-down point, walking one child per level from this one.
   *
   * A point is a path of child **bin numbers** (ROOT's 1-based bins, one per level), which is how the
   * server records where a drill landed (`NRouteContext::SetStatePoint`). This is the walk
   * `NGnNavigator::Draw` and the tools make for their layers, as one call.
   *
   * It stops where the point stops being walkable - a bin with no child, or a shorter point - and returns
   * the deepest navigator it reached, never null: a point that leads nowhere gives this navigator back, so
   * a caller that only wants "the layer this point is on" needs no check. An empty point gives this one.
   *
   * @param point Child bin numbers, one per level (empty = this navigator).
   * @return The navigator the point reaches.
   */
  NGnNavigator * GetNavigator(std::vector<int> point) const;

  /**
   * @brief Get the first child this navigator has, and the bin index it sits at.
   *
   * A child is built only for a bin that holds data, so the first bin (1) may be absent: this is the
   * level's first bin that does have a child, which is what a drill that names no bin should land on.
   *
   * @param bin Set to the child's index when one is found (untouched otherwise).
   * @return Pointer to the first child NGnNavigator, or nullptr when this navigator has none.
   */
  NGnNavigator * GetFirstChild(size_t * bin = nullptr) const;

  /**
   * Returns a pointer to a child NGnNavigator object specified by a sequence of coordinate vectors.
   * Each inner vector in the coords parameter represents a set of indices at a particular hierarchy level.
   * Traverses the hierarchy according to the provided coordinates.
   * If the specified child does not exist, returns nullptr.
   *
   * @param coords A vector of vectors of size_t, each representing coordinates at each hierarchy level.
   * @return Pointer to the child NGnNavigator, or nullptr if not found.
   */
  NGnNavigator * GetChild(std::vector<std::vector<size_t>> coords) const;

  /**
   * @brief Set child navigator at index.
   * @param child Pointer to child NGnNavigator.
   * @param index Index to set (-1 for append).
   */
  void SetChild(NGnNavigator * child, int index = -1);

  /**
   * @brief Hand over this navigator's children and forget them, without deleting them.
   *
   * Used when a subtree built in isolation (on a worker) is attached to the real navigator: the caller
   * takes the children out keyed by the projection index they sit at, and the emptied node is discarded.
   * The returned vector keeps the same indexing as `GetChild`; entries are null where there was no child.
   *
   * @return The navigator's children, by index.
   */
  std::vector<NGnNavigator *> TakeChildren();

  /**
   * @brief Get parent navigator.
   * @return Pointer to parent NGnNavigator.
   */
  NGnNavigator * GetParent() const { return fParent; }

  /**
   * @brief Returns the root parent of this NGnNavigator.
   *
   * Traverses up the parent hierarchy and returns a pointer to the root NGnNavigator.
   * If this navigator has no parent, returns itself.
   *
   * @return NGnNavigator* Pointer to the root parent navigator.
   */
  NGnNavigator * GetRoot() const;

  /**
   * @brief Set parent navigator.
   * @param parent Pointer to parent NGnNavigator.
   */
  void SetParent(NGnNavigator * parent) { fParent = parent; }

  /**
   * @brief Get object content map.
   * @return Map of object names to vectors of TObject pointers.
   */
  std::map<std::string, std::vector<TObject *>> GetObjectContentMap() const { return fObjectContentMap; }

  /**
   * @brief Resize object content map for a given name.
   * @param name Object name.
   * @param n New size.
   */
  void ResizeObjectContentMap(const std::string & name, size_t n) { fObjectContentMap[name].resize(n); };

  /**
   * @brief Get objects by name.
   * @param name Object name.
   * @return Vector of TObject pointers.
   */
  std::vector<TObject *> GetObjects(const std::string & name) const;

  /**
   * @brief Get object by name and index.
   * @param name Object name.
   * @param index Object index (default: 0).
   * @return Pointer to TObject.
   */
  TObject * GetObject(const std::string & name, int index = 0) const;

  /**
   * @brief Set object by name and index.
   * @param name Object name.
   * @param obj Pointer to TObject.
   * @param index Index to set (-1 for append).
   */
  void SetObject(const std::string & name, TObject * obj, int index = -1);

  /**
   * @brief Set object types managed by navigator.
   * @param types Vector of object type strings.
   */
  void SetObjectTypes(const std::vector<std::string> & types) { fObjectTypes = types; }

  /**
   * @brief Get object names managed by navigator.
   * @return Vector of object names.
   */
  std::vector<std::string> GetObjectNames() const { return fObjectNames; }

  /**
   * @brief Set object names managed by navigator.
   * @param names Vector of object names.
   */
  void SetObjectNames(const std::vector<std::string> & names) { fObjectNames = names; }

  /**
   * @brief Get parameter content map.
   * @return Map of parameter names to vectors of double values.
   */
  std::map<std::string, std::vector<double>> GetParameterContentMap() const { return fParameterContentMap; }

  /**
   * @brief Resize parameter content map for a given name.
   * @param name Parameter name.
   * @param n New size.
   */
  void ResizeParameterContentMap(const std::string & name, int n) { fParameterContentMap[name].resize(n); };

  /**
   * @brief Get parameters by name.
   * @param name Parameter name.
   * @return Vector of parameter values.
   */
  std::vector<double> GetParameters(const std::string & name) const;

  /**
   * @brief Get parameter value by name and index.
   * @param name Parameter name.
   * @param index Parameter index (default: 0).
   * @return Parameter value.
   */
  double GetParameter(const std::string & name, int index = 0) const;

  /**
   * @brief Set parameter value by name and index.
   * @param name Parameter name.
   * @param value Value to set.
   * @param index Index to set (-1 for append).
   */
  void SetParameter(const std::string & name, double value, int index = -1);

  /**
   * @brief Returns the map containing parameter error vectors for each parameter name.
   * @return A map from parameter names to their corresponding error vectors.
   */
  std::map<std::string, std::vector<double>> GetParameterErrorContentMap() const { return fParameterErrorContentMap; }

  /**
   * @brief Resizes the error vector for a given parameter name.
   * @param name The name of the parameter.
   * @param n The new size for the error vector.
   */
  void ResizeParameterErrorContentMap(const std::string & name, int n) { fParameterErrorContentMap[name].resize(n); };

  /**
   * @brief Retrieves the error vector for a given parameter name.
   * @param name The name of the parameter.
   * @return A vector of error values for the parameter.
   */
  std::vector<double> GetParameterErrors(const std::string & name) const;

  /**
   * @brief Retrieves a specific error value for a parameter.
   * @param name The name of the parameter.
   * @param index The index of the error value (default is 0).
   * @return The error value at the specified index.
   */
  double GetParameterError(const std::string & name, int index = 0) const;

  /**
   * @brief Sets a specific error value for a parameter.
   * @param name The name of the parameter.
   * @param value The error value to set.
   * @param index The index at which to set the error value (default is -1, which may indicate appending or special
   * handling).
   */
  void SetParameterError(const std::string & name, double value, int index = -1);
  /**
   * @brief Get parameter names managed by navigator.
   * @return Vector of parameter names.
   */
  std::vector<std::string> GetParameterNames() const { return fParameterNames; }

  /**
   * @brief Set parameter names managed by navigator.
   * @param names Vector of parameter names.
   */
  void SetParameterNames(const std::vector<std::string> & names) { fParameterNames = names; }

  /**
   * @brief Get projection histogram.
   * @return Pointer to TH1 histogram.
   */
  TH1 * GetProjection() const { return fProjection; }

  /**
   * @brief Set projection histogram.
   * @param h Pointer to TH1 histogram.
   */
  void SetProjection(TH1 * h) { fProjection = h; }

  /**
   * @brief Get number of levels in hierarchy.
   * @return Number of levels.
   */
  size_t GetNLevels() const { return fNLevels; }

  /**
   * @brief Set number of levels in hierarchy.
   * @param n Number of levels.
   */
  void SetNLevels(size_t n) { fNLevels = n; }

  /**
   * @brief Get current level in hierarchy.
   * @return Current level.
   */
  size_t GetLevel() const { return fLevel; }

  /**
   * @brief Set current level in hierarchy.
   * @param l Level to set.
   */
  void SetLevel(size_t l) { fLevel = l; }

  /**
   * @brief Whether deeper-level parameter values/errors are averaged into higher levels on export.
   *
   * Default is true (can be set globally via the NDMSPC_EXPORT_AVERAGES environment variable).
   * Disable it for large navigators to avoid the extra JSON size and CPU cost; higher levels
   * then only carry the aggregated min/max/minE/maxE.
   *
   * @return true when averaging is enabled.
   */
  bool GetAverageParameters() const { return fAverageParameters; }

  /**
   * @brief Enable or disable averaging of deeper-level parameter values/errors on export.
   *
   * Applies to this navigator and all its descendants.
   *
   * @param averageParameters true to average (default), false to skip.
   */
  void SetAverageParameters(bool averageParameters);

  /**
   * @brief Get the current levels as a vector of vectors of integers.
   * @return A vector of vectors of int representing the levels.
   */
  std::vector<std::vector<int>> GetLevels() const { return fLevels; }

  /**
   * @brief Set the levels using a vector of vectors of integers.
   * @param levels A vector of vectors of int to set as the new levels.
   */
  void SetLevels(const std::vector<std::vector<int>> & levels) { fLevels = levels; }

  /**
   * @brief Get number of cells in projection histogram.
   * @return Number of cells.
   */
  size_t GetNCells() const { return fNCells; }

  /**
   * @brief Set number of cells in projection histogram.
   * @param n Number of cells.
   */
  void SetNCells(size_t n) { fNCells = n; }

  /**
   * @brief Get last selected index.
   * @return Last selected index.
   */
  size_t GetLastIndexSelected() const { return fLastIndexSelected; }

  /**
   * @brief Set last selected index.
   * @param idx Index to set.
   */
  void SetLastIndexSelected(size_t idx) { fLastIndexSelected = idx; }

  /**
   * @brief Get last hovered bin index.
   * @return Last hovered bin index.
   */
  size_t GetLastHoverBin() const { return fLastHoverBin; }

  /**
   * @brief Set last hovered bin index.
   * @param b Bin index to set.
   */
  void SetLastHoverBin(size_t b) { fLastHoverBin = b; }

  // /**
  //  * @brief Open navigator from file.
  //  * @param filename File name.
  //  * @param branches Branches to open.
  //  * @param treename Tree name (default: "ngnt").
  //  * @return Pointer to opened NGnNavigator.
  //  */
  // static NGnNavigator * Open(const std::string & filename, const std::string & branches = "",
  //                            const std::string & treename = "ngnt");
  //
  // /**
  //  * @brief Open navigator from TTree.
  //  * @param tree Pointer to TTree.
  //  * @param branches Branches to open.
  //  * @param file Pointer to TFile.
  //  * @return Pointer to opened NGnNavigator.
  //  */
  // static NGnNavigator * Open(TTree * tree, const std::string & branches = "", TFile * file = nullptr);

  private:
  /**
   * @brief Build the projection histogram of one reshape layer.
   *
   * A level projects a fixed set of axes, so its projection histogram is identical for every bin of
   * that level. It used to be rebuilt per bin and thrown away; this is the same code, callable once
   * per level (see `Reshape`) or once per bin (the legacy path, `NDMSPC_RESHAPE_PROJECTION=per-bin`).
   *
   * @param binningDef Binning definition holding the source content sparse.
   * @param axesIds Axis indices projected at this level, in the level's order.
   * @param ranges Axis ranges to apply to the source sparse before projecting.
   * @param rangesBase Base-axis ranges, used only to describe them in the projection title.
   * @param level Level being projected (for logging).
   * @param leafBump Also map the sparse's reserved bin 0 into the projection (leaf level only).
   * @return The projection histogram, detached from `gDirectory`, or nullptr on failure.
   */
  TH1 * BuildProjection(NBinningDef * binningDef, const std::vector<int> & axesIds,
                        const std::map<int, std::vector<int>> & ranges,
                        const std::map<int, std::vector<int>> & rangesBase, size_t level, bool leafBump);

  /**
   * @brief Locate one bin of a layer inside the layer's projection histogram.
   * @param hProj Projection histogram built by BuildProjection.
   * @param coords Coordinates of the bin within the level.
   * @param nDims Number of projected axes.
   * @return The projection bin index, or -1 when the coordinates cannot be mapped.
   */
  int ProjectionIndex(TH1 * hProj, const std::vector<int> & coords, int nDims) const;

  /**
   * @brief How many worker threads to reshape a level of `nBinsLevel` bins with.
   *
   * One per bin at most, capped by the ROOT thread-pool size, `NDMSPC_NAV_WORKERS` overriding both, and
   * 1 (the serial path) for the fork-based execution modes.
   *
   * @param nBinsLevel Number of bins in the level.
   * @return Worker count, at least 1.
   */
  static size_t ReshapeWorkerCount(size_t nBinsLevel);

  NGnTree *                                     fGnTree{nullptr};            ///<! Pointer to the NGnTree
  std::vector<std::string>                      fObjectNames{};              ///< Object names
  std::map<std::string, std::vector<TObject *>> fObjectContentMap{};         ///< Object content map
  std::vector<std::string>                      fParameterNames{};           ///< Parameter names
  std::map<std::string, std::vector<double>>    fParameterContentMap{};      ///< Parameter content map
  std::map<std::string, std::vector<double>>    fParameterErrorContentMap{}; ///< Parameter error content map
  std::vector<std::string>                      fObjectTypes{"TH1"};         ///< Object types

  NGnNavigator *              fParent{nullptr}; ///< Parent object
  std::vector<NGnNavigator *> fChildren{};      ///< Children objects

  TH1 *                         fProjection{nullptr};   ///< Projection histogram
  size_t                        fNLevels{1};            ///< Number of levels in the hierarchy
  size_t                        fLevel{0};              ///< Level of the object in the hierarchy
  std::vector<std::vector<int>> fLevels{};              ///< Levels definition
  size_t                        fNCells{0};             ///< Number of cells in the projection histogram
  size_t                        fLastHoverBin{0};       ///< To avoid spamming the console on hover
  size_t                        fLastIndexSelected{0};  ///< last selected index in the object
  Int_t                         fTrigger{kButton1Down}; ///< last triggered event

  bool fAverageParameters{true}; ///< Average deeper-level parameter values/errors on export

  /// \cond CLASSIMP
  ClassDefOverride(NGnNavigator, 3);
  /// \endcond;
};
} // namespace Ndmspc
#endif
