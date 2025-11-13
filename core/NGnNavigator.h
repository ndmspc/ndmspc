#ifndef Ndmspc_NGnNavigator_H
#define Ndmspc_NGnNavigator_H
#include <TNamed.h>
#include "NBinningDef.h"
#include "NGnTree.h"
namespace Ndmspc {

///
/// \class NGnNavigator
///
/// \brief NGnNavigator object
///	\author Martin Vala <mvala@cern.ch>
///

class NGnNavigator : public TNamed {
  public:
  NGnNavigator(const char * name = "GnNavigator", const char * title = "Gn Navigator",
               std::vector<std::string> objectTypes = {"TH1"});
  virtual ~NGnNavigator();

  NGnNavigator * Reshape(std::string binningName, std::vector<std::vector<int>> levels, int level = 0,
                         std::map<int, std::vector<int>> ranges = {}, std::map<int, std::vector<int>> rangesBase = {});
  NGnNavigator * Reshape(NBinningDef * binningDef, std::vector<std::vector<int>> levels, int level = 0,
                         std::map<int, std::vector<int>> ranges = {}, std::map<int, std::vector<int>> rangesBase = {},
                         NGnNavigator * parent = nullptr);

  void Export(const std::string & filename, std::vector<std::string> objectNames, const std::string & wsUrl = "");
  void ExportToJson(json & j, NGnNavigator * obj, std::vector<std::string> objectNames);

  virtual void Print(Option_t * option = "") const override;
  virtual void Draw(Option_t * option = "") override;
  virtual void DrawSpectra(std::string parameterName, Option_t * option = "", std::vector<int> projIds = {}) const;
  virtual void Paint(Option_t * option = "") override;
  Int_t        DistancetoPrimitive(Int_t px, Int_t py) override;
  virtual void ExecuteEvent(Int_t event, Int_t px, Int_t py) override;

  NGnTree *                                     GetGnTree() const { return fGnTree; }
  void                                          SetGnTree(NGnTree * tree) { fGnTree = tree; }
  std::vector<NGnNavigator *>                   GetChildren() const { return fChildren; }
  void                                          SetChildrenSize(int n) { fChildren.resize(n); }
  NGnNavigator *                                GetChild(int index) const;
  void                                          SetChild(NGnNavigator * child, int index = -1);
  NGnNavigator *                                GetParent() const { return fParent; }
  void                                          SetParent(NGnNavigator * parent) { fParent = parent; }
  std::map<std::string, std::vector<TObject *>> GetObjectContentMap() const { return fObjectContentMap; }
  void                   ResizeObjectContentMap(const std::string & name, int n) { fObjectContentMap[name].resize(n); };
  std::vector<TObject *> GetObjects(const std::string & name) const;
  TObject *              GetObject(const std::string & name, int index = 0) const;
  void                   SetObject(const std::string & name, TObject * obj, int index = -1);

  void                     SetObjectTypes(const std::vector<std::string> & types) { fObjectTypes = types; }
  std::vector<std::string> GetObjectNames() const { return fObjectNames; }
  void                     SetObjectNames(const std::vector<std::string> & names) { fObjectNames = names; }
  std::map<std::string, std::vector<double>> GetParameterContentMap() const { return fParameterContentMap; }
  void ResizeParameterContentMap(const std::string & name, int n) { fParameterContentMap[name].resize(n); };
  std::vector<double>      GetParameters(const std::string & name) const;
  double                   GetParameter(const std::string & name, int index = 0) const;
  void                     SetParameter(const std::string & name, double value, int index = -1);
  std::vector<std::string> GetParameterNames() const { return fParameterNames; }
  void                     SetParameterNames(const std::vector<std::string> & names) { fParameterNames = names; }

  TH1 * GetProjection() const { return fProjection; }
  void  SetProjection(TH1 * h) { fProjection = h; }
  Int_t GetNLevels() const { return fNLevels; }
  void  SetNLevels(Int_t n) { fNLevels = n; }
  Int_t GetLevel() const { return fLevel; }
  void  SetLevel(Int_t l) { fLevel = l; }
  Int_t GetNCells() const { return fNCells; }
  void  SetNCells(Int_t n) { fNCells = n; }
  Int_t GetLastIndexSelected() const { return fLastIndexSelected; }
  void  SetLastIndexSelected(Int_t idx) { fLastIndexSelected = idx; }
  Int_t GetLastHoverBin() const { return fLastHoverBin; }
  void  SetLastHoverBin(Int_t b) { fLastHoverBin = b; }

  static NGnNavigator * Open(const std::string & filename, const std::string & branches = "",
                             const std::string & treename = "hnst");
  static NGnNavigator * Open(TTree * tree, const std::string & branches = "", TFile * file = nullptr);

  private:
  NGnTree *                                     fGnTree{nullptr};       ///< Pointer to the NGnTree
  std::vector<std::string>                      fObjectNames{};         ///< Object names
  std::map<std::string, std::vector<TObject *>> fObjectContentMap{};    ///< Object content map
  std::vector<std::string>                      fParameterNames{};      ///< Parameter names
  std::map<std::string, std::vector<double>>    fParameterContentMap{}; ///< Parameter content map
  std::vector<std::string>                      fObjectTypes{"TH1"};    ///< Object types

  NGnNavigator *              fParent{nullptr}; ///< Parent object
  std::vector<NGnNavigator *> fChildren{};      ///< Children objects

  TH1 * fProjection{nullptr};  ///< Projection histogram
  Int_t fNLevels{1};           ///< Number of levels in the hierarchy
  Int_t fLevel{0};             ///< Level of the object in the hierarchy
  Int_t fNCells{0};            ///< Number of cells in the projection histogram
  Int_t fLastHoverBin{0};      ///< To avoid spamming the console on hover
  Int_t fLastIndexSelected{0}; ///< last selected index in the object

  /// \cond CLASSIMP
  ClassDefOverride(NGnNavigator, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
