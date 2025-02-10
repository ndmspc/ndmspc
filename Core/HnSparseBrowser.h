#ifndef NdmspcCoreHnSparseBrowser_H
#define NdmspcCoreHnSparseBrowser_H

#include <TObject.h>
#include <THnSparse.h>
#include <TList.h>
#include <TVirtualPad.h>

namespace Ndmspc {

///
/// \class HnSparseBrowser
///
/// \brief HnSparseBrowser object
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparseBrowser : public TObject {
  public:
  HnSparseBrowser();
  virtual ~HnSparseBrowser();
  // virtual void Draw(Option_t * option = "");
  virtual int DrawBrowser(
      std::string filename =
          "root://eos.ndmspc.io//eos/ndmspc/scratch/alice/hyperloop/PWGLF-376/222580/AnalysisResults.root",
      std::string objects  = "phianalysis-t-hn-sparse_default/unlikepm,phianalysis-t-hn-sparse_default/likepp",
      std::string dirtoken = "/");

  protected:
  /// Input HnSparse
  THnSparse * fInputHnSparse{nullptr};
  /// List of hn sparce objects
  TList * fListOfHnSparses{nullptr};

  public:
  void HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);

  /// \cond CLASSIMP
  ClassDef(HnSparseBrowser, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif /* PointRun_H */
