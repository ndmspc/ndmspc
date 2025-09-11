#include <vector>
#include <TAxis.h>
#include "NLogger.h"
#include "NUtils.h"
#include "NBinning.h"
#include "NBinningPoint.h"

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
  fBaseBinMin         = new Int_t[fNDimensions];
  fBaseBinMax         = new Int_t[fNDimensions];
  fLabels.resize(fNDimensions);

  Reset();
}
NBinningPoint::~NBinningPoint()
{
  delete[] fContentCoords;
  delete[] fStorageCoords;
  delete[] fMins;
  delete[] fMaxs;
  delete[] fBaseBinMin;
  delete[] fBaseBinMax;
  fLabels.clear();
}

void NBinningPoint::Reset()
{
  ///
  /// Reset the point coordinates
  ///
  if (fContentNDimensions <= 0 || fNDimensions <= 0 || fContentCoords == nullptr) {
    NLogger::Error("NBinningPoint::Reset: Invalid dimensions or coordinates");
    return;
  }
  for (Int_t i = 0; i < fContentNDimensions; ++i) {
    fContentCoords[i] = 0;
  }
  for (Int_t i = 0; i < fNDimensions; ++i) {
    fStorageCoords[i] = 0;
    fMins[i]          = 0;
    fMaxs[i]          = 0;
    fBaseBinMin[i]    = 0;
    fBaseBinMax[i]    = 0;
    fLabels[i]        = "";
  }
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
    fBaseBinMin[i] = axisRanges[i][1];
    fBaseBinMax[i] = axisRanges[i][2];
    fMins[i]       = axis->GetBinLowEdge(axisRanges[i][1]);
    fMaxs[i]       = axis->GetBinUpEdge(axisRanges[i][2]);
    fLabels[i]     = axis->GetBinLabel(i + 1);
    // NLogger::Debug("XXXXX Axis %d: min=%.3f max=%.3f label='%s' %d %d", i, fMins[i], fMaxs[i], fLabels[i].c_str(),
    //                axis->GetNbins(), axisStorage->FindBin((fMins[i] + fMaxs[i]) / 2.0));
    fStorageCoords[i] = axisStorage->FindBin((fMins[i] + fMaxs[i]) / 2.0);
    // NLogger::Debug("  Storage bin: %d", fStorageCoords[i]);
  }
}

std::map<int, std::vector<int>> NBinningPoint::GetBaseAxisRanges() const
{
  ///
  /// Get base axis ranges as map of axis id to vector of {min, max}
  ///
  std::map<int, std::vector<int>> axisRanges;
  for (Int_t i = 0; i < fNDimensions; ++i) {
    axisRanges[i] = {fBaseBinMin[i], fBaseBinMax[i]};
  }
  return axisRanges;
}

std::string NBinningPoint::GetTitle(const std::string & prefix) const
{

  ///
  /// Returns point title
  ///

  std::string title = !prefix.empty() ? prefix + " " : "";
  for (int i = 0; i < fNDimensions; i++) {
    TAxis * a = fBinning->GetAxes()[i];
    if (a == nullptr) {
      NLogger::Error("NBinningPoint::GetTitle: Axis %d is nullptr !!!", i);
      continue;
    }

    if (a->GetNbins() > 1) {
      title += TString::Format("%s[%.3f,%.3f] ", a->GetName(), a->GetBinLowEdge(fBaseBinMin[i]),
                               a->GetBinUpEdge(fBaseBinMax[i]))
                   .Data();
    }
  }
  if (title.back() == ' ') {
    title.pop_back(); // remove last space
  }
  return title;
}
Long64_t NBinningPoint::Fill()
{
  ///
  /// Fill the content histogram at the current point
  ///
  if (fBinning == nullptr || fBinning->GetContent() == nullptr) {
    NLogger::Error("NBinningPoint::Fill: Binning or content is nullptr");
    return -1;
  }

  // TODO: Find more efficient way to verify if bin exists
  Long64_t bin = fBinning->GetContent()->GetBin(fContentCoords, kFALSE);
  if (bin < 0) {
    NLogger::Error("NBinningPoint::Fill: Bin for content already exists for coordinates: %s",
                   NUtils::GetCoordsString(NUtils::ArrayToVector(fContentCoords, fContentNDimensions)).c_str());
    return -1;
  }
  bin = fBinning->GetContent()->GetBin(fContentCoords, kTRUE);
  fBinning->GetContent()->SetBinContent(fContentCoords, 1);

  return bin;
}

bool NBinningPoint::SetPointContentFromLinearIndex(Long64_t linBin)
{
  ///
  /// Set point content coordinates from linear index
  ///
  if (fBinning == nullptr || fBinning->GetContent() == nullptr) {
    NLogger::Error("NBinningPoint::SetPointContentFromLinearIndex: Binning or content is nullptr");
    return false;
  }

  if (linBin < 0 || linBin >= fBinning->GetContent()->GetNbins()) {
    NLogger::Error("NBinningPoint::SetPointContentFromLinearIndex: Invalid linear bin index: %lld", linBin);
    return false;
  }

  fBinning->GetContent()->GetBinContent(linBin, fContentCoords);
  RecalculateStorageCoords();

  return true;
}

} // namespace Ndmspc
