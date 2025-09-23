#ifndef Ndmspc_NBinning_H
#define Ndmspc_NBinning_H
#include <TObject.h>
#include <THnSparse.h>
#include <TAxis.h>
#include <vector>
#include <map>
#include "TObjArray.h"
#include "NBinningDef.h"
#include "NBinningPoint.h"
#include "RtypesCore.h"

namespace Ndmspc {

///
/// \class NBinning
///
/// \brief NBinning object
///	\author Martin Vala <mvala@cern.ch>
///

enum class Binning {
  kSingle    = 0, ///< No rebin possible
  kMultiple  = 1, ///< Rebin is possible
  kUser      = 2, ///< No rebin possible, but axis range is as single bin (User must set bin as needed)
  kUndefined = 3  ///< Undefined binning type
};

enum class AxisType {
  kFixed     = 0, ///< fixed axis type
  kVariable  = 1, ///< variable axis type
  kUndefined = 2, ///< Undefined axis type
};

class NBinning : public TObject {
  public:
  NBinning();
  NBinning(TObjArray * axes);
  NBinning(std::vector<TAxis *> axes);
  virtual ~NBinning();

  void Initialize();

  virtual void                  Print(Option_t * option = "") const;
  void                          PrintContent(Option_t * option = "") const;
  Long64_t                      FillAll(std::vector<Long64_t> & ids);
  std::vector<std::vector<int>> GetCoordsRange(std::vector<int> c) const;
  std::vector<std::vector<int>> GetAxisRanges(std::vector<int> c) const;
  bool                          GetAxisRange(int axisId, double & min, double & max, std::vector<int> c) const;
  bool                          GetAxisRangeInBase(int axisId, int & min, int & max, std::vector<int> c) const;
  TObjArray *                   GenerateListOfAxes();
  std::vector<int>              GetAxisBinning(int axisId, const std::vector<int> & c) const;
  std::vector<int>              GetAxisIndexes(AxisType type) const;
  std::vector<int>              GetAxisIndexesByNames(std::vector<std::string> axisNames) const;
  std::vector<std::string>      GetAxisNamesByType(AxisType type) const;
  std::vector<std::string>      GetAxisNamesByIndexes(std::vector<int> axisIds) const;

  bool AddBinning(int id, std::vector<int> binning, int n = 1);
  bool AddBinningVariable(int id, std::vector<int> mins);
  bool AddBinningViaBinWidths(int id, std::vector<std::vector<int>> widths);
  bool SetAxisType(int id, AxisType type);

  /// Returns the mapping histogram
  THnSparse *              GetMap() const { return fMap; }
  THnSparse *              GetContent() const { return fContent; }
  std::vector<TAxis *>     GetAxes() const { return fAxes; }
  std::vector<TAxis *>     GetAxesByType(AxisType type) const;
  Binning                  GetBinningType(int i) const;
  AxisType                 GetAxisType(int i) const;
  char                     GetAxisTypeChar(int i) const;
  NBinningDef *            GetDefinition(const std::string & name = "");
  std::vector<std::string> GetDefinitionNames() const { return fDefinitionNames; }
  std::string              GetCurrentDefinitionName() const { return fCurrentDefinitionName; }
  // TODO: update point with current definition
  void SetCurrentDefinitionName(const std::string & name);

  void AddBinningDefinition(std::string name, std::map<std::string, std::vector<std::vector<int>>> binning,
                            bool forceDefault = false);

  NBinningPoint * GetPoint();
  NBinningPoint * GetPoint(int id, const std::string binning = "");
  void            SetPoint(NBinningPoint * point) { fPoint = point; }
  bool            SetCfg(const json & cfg);

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

  // TODO: remove
  // std::map<std::string, std::vector<std::vector<int>>> fDefinition; ///< Binning mapping definition

  /// \cond CLASSIMP
  ClassDef(NBinning, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
