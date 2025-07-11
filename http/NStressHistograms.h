#ifndef NdmspcNStressHistograms_H
#define NdmspcNStressHistograms_H
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
/// \class NStressHistograms
///
/// \brief NStressHistograms object
///	\author Martin Vala <mvala@cern.ch>
///

class NWsHandler;
class NStressHistograms : public TObject {
  public:
  NStressHistograms(int fill = 1, Long64_t reset = 1e2, int seed = 0, bool batch = false);
  virtual ~NStressHistograms();

  bool HandleEvent(NWsHandler * ws = nullptr);

  private:
  TCanvas *   fCanvas{nullptr};
  TObjArray * fObjs{nullptr};
  TH1F *      fHpx{nullptr};
  TH2F *      fHpxpy{nullptr};
  TH2F *      fHpxpz{nullptr};
  TH3F *      fHpxpypz{nullptr};
  TRandom3    fRandom;
  int         fNFill{1};
  Long64_t    fNEvents{0};
  Long64_t    fReset{static_cast<Long64_t>(1e2)};
  bool        fBatch{false};

  /// \cond CLASSIMP
  ClassDef(NStressHistograms, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
