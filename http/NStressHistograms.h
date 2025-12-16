#ifndef NdmspcNStressHistograms_H
#define NdmspcNStressHistograms_H
#include <TObject.h>
#include <TRandom3.h>

class TCanvas;
class TObjArray;
class TH1F;
class TH2F;
class TH3F;
namespace Ndmspc {

/**
 * @class NStressHistograms
 * @brief Provides stress test histograms for HTTP/WebSocket server.
 *
 * NStressHistograms manages several ROOT histograms and a random number generator
 * to simulate and visualize stress test data. It supports event handling and
 * configurable fill/reset parameters for testing server performance.
 *
 * @author Martin Vala <mvala@cern.ch>
 */
class NWsHandler;
class NStressHistograms : public TObject {
  public:
  /**
   * @brief Constructs a new NStressHistograms instance.
   * @param fill Number of fills per event (default: 1).
   * @param reset Number of events before reset (default: 1e2).
   * @param seed Random seed (default: 0).
   * @param batch If true, enables batch mode (default: false).
   */
  NStressHistograms(int fill = 1, Long64_t reset = 1e2, int seed = 0, bool batch = false);

  /**
   * @brief Destroys the NStressHistograms instance.
   */
  virtual ~NStressHistograms();

  /**
   * @brief Handles a stress test event, filling histograms.
   * @param ws Optional pointer to NWsHandler for event context.
   * @return True if event handled successfully, false otherwise.
   */
  bool HandleEvent(NWsHandler * ws = nullptr);

  private:
  TCanvas *   fCanvas{nullptr};                   ///< Canvas for histogram display
  TObjArray * fObjs{nullptr};                     ///< Array of histogram objects
  TH1F *      fHpx{nullptr};                      ///< 1D histogram
  TH2F *      fHpxpy{nullptr};                    ///< 2D histogram (x, y)
  TH2F *      fHpxpz{nullptr};                    ///< 2D histogram (x, z)
  TH3F *      fHpxpypz{nullptr};                  ///< 3D histogram (x, y, z)
  TRandom3    fRandom;                            ///< Random number generator
  int         fNFill{1};                          ///< Number of fills per event
  Long64_t    fNEvents{0};                        ///< Event counter
  Long64_t    fReset{static_cast<Long64_t>(1e2)}; ///< Reset threshold
  bool        fBatch{false};                      ///< Batch mode flag

  /// \cond CLASSIMP
  ClassDef(NStressHistograms, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
