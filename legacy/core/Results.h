#ifndef NdmspcCoreResults_H
#define NdmspcCoreResults_H
#include <TObject.h>
#include <string>
#include <vector>
#include <map>

class TFile;
class TH1;
class THnSparse;
namespace Ndmspc {

///
/// \class Results
///
/// \brief Results object
///	\author Martin Vala <mvala@cern.ch>
///
enum DataSource { simple, histogram };
class Results : public TObject {
  public:
  Results();
  virtual ~Results();

  // bool LoadConfig(std::string configfilename = "config.json", std::string userconfig = "", std::string environment =
  // "",
  //                 std::string userConfigRaw = "");
  bool LoadResults();
  /// /// Sets filename
  /// void SetFileName(std::string filename) { fInputFileName = filename; }
  /// /// Returns number of cuts
  /// int  GetNCuts() { return fCuts.size(); }
  /// void GenerateTitle();
  /// bool ApplyPoints();
  ///
  /// virtual void Draw(Option_t * option = "");
  virtual void Print(Option_t * option = "") const;
  ///
  /// private:
  /// std::string                                     fInputFileName{""};               ///< file name
  /// TFile *                                         fInputFile{nullptr};              ///< input file
  /// std::string                                     fResultsHnSparseName{"results"};  ///< results object name
  /// THnSparse *                                     fResultHnSparse{nullptr};         ///< results sparse histogram
  /// std::string                                     fResultFileName{"results.root"};  ///< results file name
  /// TH1 *                                           fMapAxesType{nullptr};            ///< map axes type
  /// std::string                                     fMapAxesTypeName{"mapAxesType"};  ///< map axes type name
  /// DataSource                                      fDataSource{simple};              ///< data source
  /// std::string                                     fParametesAxisName{"parameters"}; ///< parameters axis name
  /// std::string                                     fCurrentParameterName{""};        ///< current parameter name
  /// std::string                                     fMapTitle{""};                    ///< map type title
  /// std::vector<std::string>                        fCuts;                            ///< cuts
  /// std::vector<int>                                fPoint;                           ///< point
  /// std::vector<std::string>                        fAxes;                            ///< axes names
  /// std::vector<std::string>                        fAxesTypes;                       ///< axes types
  /// std::map<std::string, std::vector<std::string>> fAxesLabels;                      ///< axes labels
  /// std::map<std::string, int>                      fAxesBinSizes;                    ///< axes map
  ///
  /// \cond CLASSIMP
  ClassDef(Results, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
