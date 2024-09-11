#ifndef PointRun_H
#define PointRun_H

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

  bool                     Run(std::string filename, bool show = false, std::string outfilename = "");
  json &                   Cfg() { return fCfg; }
  TList *                  InputObjects() const { return fInputObjects; }
  THnSparse *              FinalResults() const { return fFinalResults; }
  Int_t *                  CurrentPoint() { return fCurrentPoint; }
  std::vector<std::string> CurrentPointLabels() { return fCurrentPointLabels; }
  json                     GetCurrentPointValue() { return fCurrentPointValue; }
  TList *                  GetOutputList() const { return fOutputList; }
  void                     SetOutputList(TList * outList) { fOutputList = outList; }
  void                     SetSkipCurrentBin(bool scb = true) { fIsSkipBin = scb; }
  void                     SetProcessExit(bool pe = true) { fIsProcessExit = pe; }

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
  })"_json};        /// Config
  TMacro                   fMacro{};    /// Macro
  int                      fVerbose{0}; /// Verbose level
  int                      fBinCount;   /// Bin Count (TODO! rename to axis level maybe)
  TFile *                  fInputFile{nullptr};
  TList *                  fInputObjects{nullptr};
  THnSparse *              fFinalResults{nullptr};
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

  bool        LoadConfig(std::string filename, bool show = false, std::string outfilename = "");
  bool        Init(std::string extraPath = "");
  bool        Finish();
  TList *     OpenInputs();
  THnSparse * CreateResult();
  bool        NdmspcApplyCuts();
  void        NdmspcRebinBins(int & min, int & max, int rebin = 1);

  int  ProcessSingleFile();
  bool NdmspcProcessSinglePoint();
  bool NdmspcProcessRecursive(int i);
  bool NdmspcProcessRecursiveInner(Int_t i, std::vector<std::string> & n);
  void NdmspcOutputFileOpen();
  void NdmspcOutputFileClose();
  int  NdmspcProcessHistogramRun();

  /// \cond CLASSIMP
  ClassDef(PointRun, 1);
  /// \endcond;
};
} // namespace NdmSpc
#endif /* PointRun_H */
