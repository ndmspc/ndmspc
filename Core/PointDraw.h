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
namespace Ndmspc {

///
/// \class PointDraw
///
/// \brief PointDraw object
///	\author Martin Vala <mvala@cern.ch>
///

class PointDraw : public TObject {
  public:
  PointDraw();
  virtual ~PointDraw();

  int DrawPoint(int level, std::string config = "myAnalysis.json", std::string userConfig = "",
                std::string environment = "", std::string userConfigRaw = "", std::string binning = "");

  protected:
  TFile *                  fIn             = nullptr;    ///< Input file
  THnSparse *              fResultHnSparse = nullptr;    ///< Result HnSparse
  std::string              fCurrentParameterName;        ///< Current parameter name
  std::string              fCurrentContentPath;          ///< Current content path
  std::vector<int>         fParameterPoint;              ///< Parameter point
  std::vector<int>         fProjectionAxes;              ///< Projection axes
  TH1 *                    fMapAxesType = nullptr;       ///< Map axes type
  std::string              fMapTitle;                    ///< Map title
  TH1 *                    fParamMapHistogram = nullptr; ///< Param map histogram
  int                      fNDimCuts          = 0;       ///< Number of dimension cuts
  std::vector<std::string> fData{};                      ///< Data
  std::vector<int>         fDataId{};                    ///< Data ids
  std::vector<std::string> fMc{};                        ///< MC
  std::vector<int>         fMcId{};                      ///< MC ids
  static std::string       fgEnvironment;                ///< Currnet environment

  public:
  void HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void HighlightData(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void HighlightProjectionPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
  void UpdateRanges();
  void DrawProjections(bool ignoreMapping = false);
  void DrawUser();
  /// Sets environment
  static void SetEnvironment(std::string env) { fgEnvironment = env; }

  /// \cond CLASSIMP
  ClassDef(PointDraw, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif /* PointRun_H */
