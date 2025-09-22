#ifndef Ndmspc_NBinningPoint_H
#define Ndmspc_NBinningPoint_H
#include <TObject.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Ndmspc {

///
/// \class NBinningPoint
///
/// \brief NBinningPoint object
///	\author Martin Vala <mvala@cern.ch>
///
class NBinning;
class NStorageTree;
class NBinningPoint : public TObject {
  public:
  NBinningPoint(NBinning * b = nullptr);
  virtual ~NBinningPoint();

  virtual void Print(Option_t * option = "") const;
  virtual void Reset();

  Int_t   GetNDimensions() const { return fNDimensions; }
  Int_t * GetCoords() const { return fContentCoords; }
  void    RecalculateStorageCoords();

  json & GetCfg() { return fCfg; }
  void   SetCfg(const json & cfg) { fCfg = cfg; }

  Long64_t Fill();

  bool SetPointContentFromLinearIndex(Long64_t linBin);

  std::map<int, std::vector<int>> GetBaseAxisRanges() const;
  std::string                     GetTitle(const std::string & prefix = "") const;

  NBinning *     GetBinning() const { return fBinning; }
  void           SetBinning(NBinning * b);
  NStorageTree * GetTreeStorage() const { return fTreeStorage; }
  void           SetTreeStorage(NStorageTree * s) { fTreeStorage = s; }

  private:
  json                     fCfg{};                  ///< Configuration object
  NBinning *               fBinning{nullptr};       ///< Binning object
  NStorageTree *           fTreeStorage{nullptr};   ///< Storage tree object
  Int_t                    fContentNDimensions{1};  ///< Number of dimensions in content histogram
  Int_t *                  fContentCoords{nullptr}; ///< Coordinates of the point
  Int_t                    fNDimensions{1};         ///< Number of dimensions
  Int_t *                  fStorageCoords{nullptr}; ///< Storage coordinates of the point
  Double_t *               fMins{nullptr};          ///< Minimum values for each axis
  Double_t *               fMaxs{nullptr};          ///< Maximum values for each axis
  Int_t *                  fBaseBinMin{nullptr};    ///< Base bin minimum (for variable binning)
  Int_t *                  fBaseBinMax{nullptr};    ///< Base bin maximum (for variable binning)
  std::vector<std::string> fLabels{};               ///< Labels for each axis

  /// \cond CLASSIMP
  ClassDef(NBinningPoint, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
