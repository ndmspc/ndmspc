#ifndef NdmspcCorePointDraw_H
#define NdmspcCorePointDraw_H

#include <TObject.h>
#include <TFile.h>
#include <THnSparse.h>
#include <TH1.h>
#include <TVirtualPad.h>
#include <TObject.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
namespace NdmSpc {

///
/// \class PointRun
///
/// \brief PointRun object
///	\author Martin Vala <mvala@cern.ch>
///

class PointDraw : public TObject {
  public:
  PointDraw();
  virtual ~PointDraw();

  int Draw(std::string config = "myAnalysis.json", std::string userConfig = "", std::string environment = "");

  protected:
  TFile *                  fIn             = nullptr;
  THnSparse *              fResultHnSparse = nullptr;
  std::string              fCurrentParameterName;
  std::string              fCurrentContentPath;
  std::vector<int>         fParameterPoint;
  std::vector<int>         fProjectionAxes;
  TH1 *                    fMapAxesType = nullptr;
  std::string              fMapTitle;
  TH1 *                    fParamMapHistogram = nullptr;
  int                      fNDimCuts          = 0;
  std::vector<std::string> fData{};
  std::vector<int>         fDataId{};
  std::vector<std::string> fMc{};
  std::vector<int>         fMcId{};
  static std::string       fgEnvironment;

  public:
  void        HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void        HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void        HighlightData(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void        HighlightProjectionPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void        UpdateRanges();
  void        DrawProjections();
  void        DrawUser();
  static void SetEnvironment(std::string env) { fgEnvironment = env; }

  /// \cond CLASSIMP
  ClassDef(PointDraw, 1);
  /// \endcond;
};
} // namespace NdmSpc
#endif /* PointRun_H */
