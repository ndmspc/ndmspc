#ifndef NdmspcCoreHnSparseBrowser_H
#define NdmspcCoreHnSparseBrowser_H

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
/// \brief PointRun objects
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparseBrowser : public TObject {
  public:
  HnSparseBrowser();
  virtual ~HnSparseBrowser();

  int Draw(std::string filename =
               "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/hyperloop/PWGLF-376/222580/AnalysisResults.root",
           std::string objects  = "phianalysis-t-hn-sparse_default/unlikepm,phianalysis-t-hn-sparse_default/likepp",
           std::string dirtoken = "/");

  protected:
  THnSparse * fInputHnSparse{nullptr};
  TList *     fListOfHnSparses{nullptr};

  public:
  void HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);

  /// \cond CLASSIMP
  ClassDef(HnSparseBrowser, 1);
  /// \endcond;
};
} // namespace NdmSpc
#endif /* PointRun_H */
