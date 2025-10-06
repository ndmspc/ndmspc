#ifndef Ndmspc_NHnSparseObject_H
#define Ndmspc_NHnSparseObject_H
#include <vector>
#include <TVirtualPad.h>
#include <TObject.h>
#include <TH1.h>
#include "NBinning.h"
#include "NGnTree.h"
#include "NUtils.h"

namespace Ndmspc {

///
/// \class NHnSparseObject
///
/// \brief NHnSparseObject object
///	\author Martin Vala <mvala@cern.ch>
///
class NHnSparseTreePoint;
class NHnSparseObject : public NGnTree {
  public:
  NHnSparseObject(std::vector<TAxis *> axes = {});
  NHnSparseObject(TObjArray * axes);
  virtual ~NHnSparseObject();

  virtual void Print(Option_t * option = "") const override;
  void         Export(std::string filename);
  void         ExportJson(json & j, NHnSparseObject * obj);
  virtual void Draw(Option_t * option = "") override;
  virtual void Paint(Option_t * option = "") override;

  Int_t        DistancetoPrimitive(Int_t px, Int_t py) override;
  virtual void ExecuteEvent(Int_t event, Int_t px, Int_t py) override;

  int Fill(TH1 * h, NHnSparseTreePoint * point = nullptr);

  std::map<std::string, std::vector<TObject *>> GetObjectContentMap() const { return fObjectContentMap; }
  void                   ResizeObjectContentMap(const std::string & name, int n) { fObjectContentMap[name].resize(n); };
  std::vector<TObject *> GetObjects(const std::string & name) const;
  TObject *              GetObject(const std::string & name, int index = 0) const;
  void                   SetObject(const std::string & name, TObject * obj, int index = -1);

  std::map<std::string, std::vector<double>> GetParameterContentMap() const { return fParameterContentMap; }
  void ResizeParameterContentMap(const std::string & name, int n) { fParameterContentMap[name].resize(n); };
  std::vector<double> GetParameters(const std::string & name) const;
  double              GetParameter(const std::string & name, int index = 0) const;
  void                SetParameter(const std::string & name, double value, int index = -1);

  NBinning * GetBinning() const { return fBinning; }

  void        SetHnSparse(THnSparse * hnSparse) { fHnSparse = hnSparse; }
  THnSparse * GetHnSparse() const { return fHnSparse; }
  void        SetProjection(TH1 * projection) { fProjection = projection; }
  TH1 *       GetProjection() const { return fProjection; }

  std::vector<NHnSparseObject *> GetChildren() const { return fChildren; }
  void                           SetChildrenSize(int n) { fChildren.resize(n); }
  NHnSparseObject *              GetChild(int index) const;
  void                           SetChild(NHnSparseObject * child, int index = -1);
  NHnSparseObject *              GetParent() const { return fParent; }
  void                           SetParent(NHnSparseObject * parent) { fParent = parent; }
  void                           SetLastHoverBin(Int_t bin) { fLastHoverBin = bin; }
  Int_t                          GetLastHoverBin() const { return fLastHoverBin; }
  void                           SetNLevels(Int_t n);
  void                           SetLevel(Int_t level) { fLevel = level; }
  void                           SetNCells(Int_t n) { fNCells = n; }
  Int_t                          GetNCells() const { return fNCells; }

  void  SetLastIndexSelected(Int_t index) { fLastIndexSelected = index; }
  Int_t GetLastIndexSelected() const { return fLastIndexSelected; }

  protected:
  std::map<std::string, std::vector<TObject *>> fObjectContentMap{};    ///< Object content map
  std::map<std::string, std::vector<double>>    fParameterContentMap{}; ///< Parameter content map

  NHnSparseObject *              fParent{nullptr};   ///< Parent object
  std::vector<NHnSparseObject *> fChildren{};        ///< Children objects
  THnSparse *                    fHnSparse{nullptr}; ///< Histogram object

  // TVirtualPad *                  fPad{nullptr};      ///< Pad for drawing

  TH1 * fProjection{nullptr}; ///< Projection histogram for 2D
  Int_t fLastHoverBin{0};     ///< To avoid spamming the console on hover
  Int_t fNLevels{1};          ///< Number of levels in the hierarchy
  Int_t fLevel{0};            ///< Level of the object in the hierarchy
  Int_t fNCells{0};           ///< Number of cells in the projection histogram
  // std::vector<NHnSparseObject *> fPoint;                ///< Stack of current pads
  Int_t fLastIndexSelected{0}; ///< last selected index in the object

  /// \cond CLASSIMP
  ClassDefOverride(NHnSparseObject, 2);
  /// \endcond;
};
} // namespace Ndmspc
#endif
