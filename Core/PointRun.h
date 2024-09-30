#ifndef NdmspcCorePointRun_H
#define NdmspcCorePointRun_H

#include <TObject.h>
#include <TMacro.h>
#include <TFile.h>
#include <TList.h>
#include <TH1S.h>
#include <THnSparse.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace NdmSpc {

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

  bool        Run(std::string filename, std::string userConfig = "", bool show = false, std::string outfilename = "");
  json &      Cfg() { return fCfg; }
  json *      CfgPtr() { return &fCfg; }
  TFile *     GetInputFile() const { return fInputFile; }
  TList *     GetInputList() const { return fInputList; }
  THnSparse * GetResultObject() const { return fResultObject; }
  Int_t *     GetCurrentPoint() { return fCurrentPoint; }
  std::vector<std::string> GetCurrentPointLabels() { return fCurrentPointLabels; }
  json                     GetCurrentPointValue() { return fCurrentPointValue; }
  TList *                  GetOutputList() const { return fOutputList; }
  void                     SetOutputList(TList * outList) { fOutputList = outList; }
  void                     SetSkipCurrentBin(bool scb = true) { fIsSkipBin = scb; }
  void                     SetProcessExit(bool pe = true) { fIsProcessExit = pe; }
  static void              SetEnvironment(std::string env) { fgEnvironment = env; }

  static bool Generate(std::string name = "myAnalysis", std::string inFile = "myFile.root",
                       std::string inObjectName = "myNDHistogram");
  static bool Merge(std::string name = "myAnalysis.json", std::string userConfig = "",
                    std::string fileOpt = "?remote=1");

  private:
  json                     fCfg{R"({
  "ndmspc": {
    "verbose": 0,
    "result": {
      "parameters": { "labels": ["p0", "p1"], "default": 0 }
    },
    "output": {
      "host": "",
      "dir": "",
      "file": "content.root",
      "delete": "onInit",
      "opt": "?remote=1"
    }
  },
  "verbose" : 0
  })"_json};            /// Config
  TMacro *                 fMacro{nullptr}; /// Macro
  int                      fVerbose{0};     /// Verbose level
  int                      fBinCount{0};    /// Bin Count (TODO! rename to axis level maybe)
  TFile *                  fInputFile{nullptr};
  TList *                  fInputList{nullptr};
  THnSparse *              fResultObject{nullptr};
  TFile *                  fCurrentOutputFile{nullptr};
  std::string              fCurrentOutputFileName{};
  TDirectory *             fCurrentOutputRootDirectory{nullptr};
  Int_t                    fCurrentPoint[32];
  std::vector<std::string> fCurrentPointLabels{};
  json                     fCurrentPointValue{};
  THnSparse *              fCurrentProccessHistogram{nullptr};
  TH1S *                   fMapAxesType{nullptr};
  std::vector<TAxis *>     fCurrentProcessHistogramAxes{};
  std::vector<int>         fCurrentProcessHistogramPoint{};
  bool                     fIsSkipBin{false};
  bool                     fIsProcessOk{false};
  bool                     fIsProcessExit{false};
  TList *                  fOutputList{nullptr};
  static std::string       fgEnvironment; /// Environment

  bool    LoadConfig(std::string config, std::string userConfig = "", bool show = false, std::string outfilename = "");
  bool    Init(std::string extraPath = "");
  bool    Finish();
  TList * OpenInputs();
  THnSparse * CreateResult();
  bool        ApplyCuts();

  int  ProcessSingleFile();
  bool ProcessSinglePoint();
  bool ProcessRecursive(int i);
  bool ProcessRecursiveInner(Int_t i, std::vector<std::string> & n);
  void OutputFileOpen();
  void OutputFileClose();
  int  ProcessHistogramRun();

  /// \cond CLASSIMP
  ClassDef(PointRun, 1);
  /// \endcond;
};
} // namespace NdmSpc
#endif /* PointRun_H */
