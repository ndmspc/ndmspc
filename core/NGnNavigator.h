#ifndef Ndmspc_NGnNavigator_H
#define Ndmspc_NGnNavigator_H
#include <TNamed.h>
#include <Buttons.h>
#include <cstddef>
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
   * @param binningName Name of binning definition.
   * @param levels Vector of levels.
   * @param level Current level (default: 0).
   * @param ranges Map of ranges for axes.
   * @param rangesBase Map of base ranges for axes.
   * @return Pointer to reshaped NGnNavigator.
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
   * @brief Export navigator data to file.
   * @param filename Output file name.
   * @param objectNames Names of objects to export.
   * @param wsUrl Optional WebSocket URL.
   * @param timeoutMs Timeout in milliseconds (default: 5000).
   */
  void Export(const std::string & filename, std::vector<std::string> objectNames, const std::string & wsUrl = "",
              int timeoutMs = 1000);

  /**
   * @brief Export navigator data to JSON.
   * @param j JSON object to fill.
   * @param obj Navigator object to export.
   * @param objectNames Names of objects to export.
   */
  void ExportToJson(json & j, NGnNavigator * obj, std::vector<std::string> objectNames);

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
   * @brief Draw spectra for a parameter.
   * @param parameterName Name of parameter.
   * @param option Draw options.
   * @param projIds Projection IDs.
   */
  virtual void DrawSpectra(std::string parameterName, std::vector<int> projIds, Option_t * option = "") const;

  /**
   * @brief Draws spectra for the given parameter and projection axes.
   *
   * @param parameterName The name of the parameter to draw spectra for.
   * @param projAxes A vector of axis names to project onto.
   * @param option Optional drawing options.
   */
  virtual void DrawSpectraByName(std::string parameterName, std::vector<std::string> projAxes,
                                 Option_t * option = "") const;

  /**
   * @brief Draws all spectra for the given parameter.
   *
   * @param parameterName The name of the parameter to draw all spectra for.
   * @param option Optional drawing options.
   */
  virtual void DrawSpectraAll(std::string parameterName, Option_t * option = "") const;

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
   * @brief Set child navigator at index.
   * @param child Pointer to child NGnNavigator.
   * @param index Index to set (-1 for append).
   */
  void SetChild(NGnNavigator * child, int index = -1);

  /**
   * @brief Get parent navigator.
   * @return Pointer to parent NGnNavigator.
   */
  NGnNavigator * GetParent() const { return fParent; }

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
  NGnTree *                                     fGnTree{nullptr};            ///<! Pointer to the NGnTree
  std::vector<std::string>                      fObjectNames{};              ///< Object names
  std::map<std::string, std::vector<TObject *>> fObjectContentMap{};         ///< Object content map
  std::vector<std::string>                      fParameterNames{};           ///< Parameter names
  std::map<std::string, std::vector<double>>    fParameterContentMap{};      ///< Parameter content map
  std::map<std::string, std::vector<double>>    fParameterErrorContentMap{}; ///< Parameter error content map
  std::vector<std::string>                      fObjectTypes{"TH1"};         ///< Object types

  NGnNavigator *              fParent{nullptr}; ///< Parent object
  std::vector<NGnNavigator *> fChildren{};      ///< Children objects

  TH1 *  fProjection{nullptr};   ///< Projection histogram
  size_t fNLevels{1};            ///< Number of levels in the hierarchy
  size_t fLevel{0};              ///< Level of the object in the hierarchy
  size_t fNCells{0};             ///< Number of cells in the projection histogram
  size_t fLastHoverBin{0};       ///< To avoid spamming the console on hover
  size_t fLastIndexSelected{0};  ///< last selected index in the object
  Int_t  fTrigger{kButton1Down}; ///< last triggered event

  /// \cond CLASSIMP
  ClassDefOverride(NGnNavigator, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
