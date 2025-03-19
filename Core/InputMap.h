#ifndef NdmspcInputMap_H
#define NdmspcInputMap_H
#include <TObject.h>
#include <string>
#include <vector>
#include <map>
#include "Utils.h"

class THnSparse;
namespace Ndmspc {

///
/// \class InputMap
///
/// \brief InputMap object
///	\author Martin Vala <mvala@cern.ch>
///

class InputMap : public TObject {
  public:
  InputMap(json config);
  InputMap(std::string mapFileName = "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/rsn/hDataMap.root",
           std::string mapObjName  = "hDataMap",
           std::string baseDir     = "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/rsn/bins",
           std::string fileName    = "AnalysisResults.root");
  virtual ~InputMap();

  virtual void                                    Print(Option_t * option = "") const;
  bool                                            LoadMap();
  THnSparse *                                     GetMap() const { return fMap; }
  THnSparse *                                     Query(const std::string & ouputPath);
  THnSparse *                                     Generate(std::vector<std::vector<int>> bins);
  std::map<std::string, std::vector<std::string>> GetProjections() const { return fProjections; }

  private:
  std::string                                     fMapFileName{}; ///< Map File name
  std::string                                     fMapObjName{};  ///< Map object
  std::string                                     fBaseDir{};     ///< Base Directory
  std::string                                     fFileName{};    ///< File name
  THnSparse *                                     fMap{nullptr};  ///< Map
  std::map<std::string, std::vector<std::string>> fProjections{};

  /// \cond CLASSIMP
  ClassDef(InputMap, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
