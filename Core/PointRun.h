#ifndef NdmspcCorePointRun_H
#define NdmspcCorePointRun_H

#include <TObject.h>
#include <TMacro.h>
#include <TFile.h>
#include <TList.h>
#include <TH1S.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
#include <vector>
#include "Core.h"
using json = nlohmann::json;

namespace Ndmspc {

///
/// \class PointRun
///
/// \brief PointRun object
///	\author Martin Vala <mvala@cern.ch>
///

class PointRun : public TObject {
  public:
  PointRun(std::string macro = "NdmspcPointRun.C");
  virtual ~PointRun();

  bool Run(std::string filename, std::string userConfig = "", std::string environment = "",
           std::string userConfigRaw = "", bool show = false, std::string outfilename = "");
  bool GenerateJobs(std::string jobs, std::string filename, std::string userConfig = "", std::string environment = "",
                    std::string userConfigRaw = "", std::string jobDir = "/tmp/ndmspc-jobs", std::string binnings = "");

  /// Returns global config object
  json & Cfg() { return gCfg; }
  /// Returns global config pointer to object
  json * CfgPtr() { return &gCfg; }
  /// Returns input file
  TFile * GetInputFile() const { return fInputFile; }
  /// Returns list of input objects
  TList * GetInputList() const { return fInputList; }
  /// Returns retults hnsparse object
  THnSparse * GetResultObject() const { return fResultObject; }
  /// Returns current point coordinates
  Int_t * GetCurrentPoint() { return fCurrentPoint; }
  /// Returns current point labels
  std::vector<std::string> GetCurrentPointLabels() { return fCurrentPointLabels; }
  /// Return current point value in json
  json GetCurrentPointValue() { return fCurrentPointValue; }
  /// Returns output list
  TList * GetOutputList() const { return fOutputList; }
  /// Sets output list
  void SetOutputList(TList * outList) { fOutputList = outList; }
  /// Sets flag for skupping bin
  void SetSkipCurrentBin(bool scb = true) { fIsSkipBin = scb; }
  /// Sets flag for ending process
  void SetProcessExit(bool pe = true) { fIsProcessExit = pe; }
  /// Sets environment
  static void SetEnvironment(std::string env) { fgEnvironment = env; }

  static bool Generate(std::string name = "myAnalysis", std::string inFile = "myFile.root",
                       std::string inObjectName = "myNDHistogram");
  /// Merge
  static bool Merge(std::string name = "myAnalysis.json", std::string userConfig = "", std::string environment = "",
                    std::string userConfigRaw = "", std::string fileOpt = "?remote=1");

  private:
  TMacro *                 fMacro{nullptr};                      ///< Macro
  int                      fVerbose{0};                          ///< Verbose level
  int                      fBinCount{0};                         ///< Bin Count (TODO! rename to axis level maybe)
  TFile *                  fInputFile{nullptr};                  ///< Input file
  TList *                  fInputList{nullptr};                  ///< Input list
  THnSparse *              fResultObject{nullptr};               ///< Result object
  TFile *                  fCurrentOutputFile{nullptr};          ///< Current output file
  std::string              fCurrentOutputFileName{};             ///< Current output filename
  TDirectory *             fCurrentOutputRootDirectory{nullptr}; ///< Current output root directory
  Int_t                    fCurrentPoint[32];                    ///< Current point
  std::vector<std::string> fCurrentPointLabels{};                ///< Current labels
  json                     fCurrentPointValue{};                 ///< Current point value
  THnSparse *              fCurrentProccessHistogram{nullptr};   ///< Current processed histogram
  TH1S *                   fMapAxesType{nullptr};                ///< Map axis type histogram
  std::vector<TAxis *>     fCurrentProcessHistogramAxes{};       ///< Current process histogram axes
  std::vector<int>         fCurrentProcessHistogramPoint{};      ///< Current process histoghram point
  bool                     fIsSkipBin{false};                    ///< Flag to skip bin
  bool                     fIsProcessOk{false};                  ///< Flag that process is ok
  bool                     fIsProcessExit{false};                ///< Flag to exit process
  TList *                  fOutputList{nullptr};                 ///< Output list
  static std::string       fgEnvironment;                        ///< Environment

  bool        LoadConfig(std::string config, std::string userConfig = "", std::string environment = "",
                         std::string userConfigRaw = "", bool show = false, std::string outfilename = "");
  bool        Init(std::string extraPath = "");
  bool        Finish();
  TList *     OpenInputs();
  THnSparse * CreateResult();
  bool        ApplyCuts();

  int  ProcessSingleFile();
  bool ProcessSinglePoint();
  bool ProcessRecursive(int i);
  bool ProcessRecursiveInner(Int_t i, std::vector<std::string> & n);
  void OutputFileOpen();
  void OutputFileClose();
  int  ProcessHistogramRun();
  bool GenerateRecursiveConfig(Int_t dim, std::vector<std::vector<int>> & ranges, json & cfg, std::string & outfilename,
                               int & count);

  /// \cond CLASSIMP
  ClassDef(PointRun, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif /* PointRun_H */
