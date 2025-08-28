#include "NBinningPoint.h"
#include <vector>
#include "TAxis.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NBinning.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NBinningPoint);
/// \endcond

namespace Ndmspc {
NBinningPoint::NBinningPoint(NBinning * b) : TObject(), fBinning(b)
{

  if (fBinning == nullptr) {
    // NLogger::Error("NBinningPoint: Binning is nullptr");
    return;
  }
  if (fBinning->GetAxes().empty()) {
    NLogger::Error("NBinningPoint: Binning has no axes");
    return;
  }
  if (fBinning->GetContent() == nullptr) {
    NLogger::Error("NBinningPoint: Binning content is nullptr");
    return;
  }

  fContentNDimensions = fBinning->GetContent()->GetNdimensions();
  fNDimensions        = fBinning->GetAxes().size();
  fContentCoords      = new Int_t[fContentNDimensions];
  fStorageCoords      = new Int_t[fNDimensions];
  fMins               = new Double_t[fNDimensions];
  fMaxs               = new Double_t[fNDimensions];
  fLabels.resize(fNDimensions);
}
NBinningPoint::~NBinningPoint()
{
  // delete[] fContentCoords;
  // delete[] fStorageCoords;
  // delete[] fMins;
  // delete[] fMaxs;
  // fLabels.clear();
}
void NBinningPoint::Print(Option_t * option) const
{
  /// Print the binning point
  ///
  if (fContentNDimensions <= 0 || fNDimensions <= 0 || fContentCoords == nullptr) {
    NLogger::Error("NBinningPoint::Print: Invalid dimensions or coordinates");
    return;
  }
  TString opt = option;
  opt.ToUpper();

  NLogger::Info("NBinningPoint: %s",
                NUtils::GetCoordsString(NUtils::ArrayToVector(fContentCoords, fContentNDimensions)).c_str());
  NLogger::Info("  Storage coordinates: %s",
                NUtils::GetCoordsString(NUtils::ArrayToVector(fStorageCoords, fNDimensions)).c_str());

  if (opt.Contains("A")) {
    for (int i = 0; i < fNDimensions; ++i) {
      NLogger::Info("  Axis %d: min=%.3f max=%.3f label='%s'", i, fMins[i], fMaxs[i], fLabels[i].c_str());
    }
  }
}

void NBinningPoint::RecalculateStorageCoords()
{
  ///
  /// Recalculate storage coordinates based on content coordinates
  ///

  if (fBinning == nullptr || fBinning->GetContent() == nullptr) {
    NLogger::Error("NBinningPoint::RecalculateStorageCoords: Binning or content is nullptr");
    return;
  }

  std::vector<int>              contentVec = NUtils::ArrayToVector(fContentCoords, fContentNDimensions);
  std::vector<std::vector<int>> axisRanges = fBinning->GetAxisRanges(contentVec);
  // for (size_t i = 0; i < axisRanges.size(); i++) {
  //   NLogger::Debug("Axis %zu: %s", i, NUtils::GetCoordsString(axisRanges[i], -1).c_str());
  // }
  for (Int_t i = 0; i < fNDimensions; ++i) {
    // fStorageCoords[i] = fBinning->GetAxes()[i]->FindBin(fContentCoords[i]);
    //   std::string rangeStr = NUtils::GetCoordsString(axisRanges[i], -1);
    TAxis * axis        = fBinning->GetAxes()[i];
    TAxis * axisStorage = (TAxis *)fBinning->GetDefinition()->GetAxes()->At(i);
    // NLogger::Debug("XXXXX Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, axis->GetName(),
    //                axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    // NLogger::Debug("XXXXX AxisStorage %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i,
    // axisStorage->GetName(),
    //                axisStorage->GetTitle(), axisStorage->GetNbins(), axisStorage->GetXmin(), axisStorage->GetXmax());
    //
    fMins[i]   = axis->GetBinLowEdge(axisRanges[i][1]);
    fMaxs[i]   = axis->GetBinUpEdge(axisRanges[i][2]);
    fLabels[i] = axis->GetBinLabel(i + 1);
    // NLogger::Debug("XXXXX Axis %d: min=%.3f max=%.3f label='%s' %d %d", i, fMins[i], fMaxs[i], fLabels[i].c_str(),
    //                axis->GetNbins(), axisStorage->FindBin((fMins[i] + fMaxs[i]) / 2.0));
    fStorageCoords[i] = axisStorage->FindBin((fMins[i] + fMaxs[i]) / 2.0);
    // NLogger::Debug("  Storage bin: %d", fStorageCoords[i]);
  }
}

} // namespace Ndmspc
