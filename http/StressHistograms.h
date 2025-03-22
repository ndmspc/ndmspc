#ifndef NdmspcStressHistograms_H
#define NdmspcStressHistograms_H
#include <TObject.h>
#include <TRandom3.h>
#include "RtypesCore.h"
class TCanvas;
class TObjArray;
class TH1F;
class TH2F;
class TH3F;
namespace Ndmspc {

///
/// \class StressHistograms
///
/// \brief StressHistograms object
///	\author Martin Vala <mvala@cern.ch>
///

class WebSocketHandler;
class StressHistograms : public TObject {
  public:
  StressHistograms(int fill = 1, Long64_t reset = 1e2, int seed = 0, bool batch = false);
  virtual ~StressHistograms();

  bool HandleEvent(WebSocketHandler * ws = nullptr);

  private:
  TCanvas *   fCanvas{nullptr};
  TObjArray * fObjs{nullptr};
  TH1F *      fHpx{nullptr};
  TH2F *      fHpxpy{nullptr};
  TH3F *      fHpxpypz{nullptr};
  TRandom3    fRandom;
  int         fNFill{1};
  Long64_t    fNEvents{0};
  Long64_t    fReset{static_cast<Long64_t>(1e2)};
  bool        fBatch{false};

  /// \cond CLASSIMP
  ClassDef(StressHistograms, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
