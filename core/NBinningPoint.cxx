#include <vector>
#include <TAxis.h>
#include "NBinningDef.h"
#include "NLogger.h"
#include "NStorageTree.h"
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
    // NLogError("NBinningPoint: Binning is nullptr");
    return;
  }
  if (fBinning->GetAxes().empty()) {
    NLogError("NBinningPoint: Binning has no axes");
    return;
  }
  if (fBinning->GetContent() == nullptr) {
    NLogError("NBinningPoint: Binning content is nullptr");
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

  NLogTrace("NBinningPoint::Reset: Resetting point ...");

  if (fContentNDimensions <= 0 || fNDimensions <= 0 || fContentCoords == nullptr) {
    NLogError("NBinningPoint::Reset: Invalid dimensions or coordinates");
    return;
  }
  for (Int_t i = 0; i < fContentNDimensions; ++i) {
    fContentCoords[i] = -1;
  }
  for (Int_t i = 0; i < fNDimensions; ++i) {
    fStorageCoords[i] = -1;
    fMins[i]          = -1;
    fMaxs[i]          = -1;
    fBaseBinMin[i]    = -1;
    fBaseBinMax[i]    = -1;
    fLabels[i]        = "";
  }
  fEntryNumber = -1;
}

void NBinningPoint::Print(Option_t * option) const
{
  /// Print the binning point
  ///
  if (fContentNDimensions <= 0 || fNDimensions <= 0 || fContentCoords == nullptr) {
    NLogError("NBinningPoint::Print: Invalid dimensions or coordinates");
    return;
  }
  TString opt = option;
  opt.ToUpper();

  NLogInfo("NBinningPoint: %s",
           NUtils::GetCoordsString(NUtils::ArrayToVector(fContentCoords, fContentNDimensions)).c_str());
  NLogInfo("  Storage coordinates: %s",
           NUtils::GetCoordsString(NUtils::ArrayToVector(fStorageCoords, fNDimensions)).c_str());
  NLogInfo("  Entry number: %lld", fEntryNumber);
  NLogInfo("  Title: '%s'", GetString().c_str());
  if (opt.Contains("C")) {
    NLogInfo("  Config: %s", fCfg.dump().c_str());
  }

  if (opt.Contains("A")) {
    for (int i = 0; i < fNDimensions; ++i) {
      NLogInfo("  Axis %d: min=%.3f max=%.3f label='%s'", i, fMins[i], fMaxs[i], fLabels[i].c_str());
    }
  }
}

bool NBinningPoint::RecalculateStorageCoords(Long64_t entry, bool useBinningDefCheck)
{
  ///
  /// Recalculate storage coordinates based on content coordinates
  ///

  if (fBinning == nullptr || fBinning->GetContent() == nullptr) {
    NLogError("NBinningPoint::RecalculateStorageCoords: Binning or content is nullptr");
    return false;
  }

  fEntryNumber                             = entry;
  std::vector<int>              contentVec = NUtils::ArrayToVector(fContentCoords, fContentNDimensions);
  std::vector<std::vector<int>> axisRanges = fBinning->GetAxisRanges(contentVec);
  // for (size_t i = 0; i < axisRanges.size(); i++) {
  //   NLogDebug("Axis %zu: %s", i, NUtils::GetCoordsString(axisRanges[i], -1).c_str());
  // }
  NBinningDef * binDef = fBinning->GetDefinition();
  if (binDef == nullptr) {
    NLogError("NBinningPoint::RecalculateStorageCoords: Binning definition is nullptr");
    return false;
  }
  std::vector<Long64_t> ids = binDef->GetIds();

  // useBinningDefCheck = false;
  if (useBinningDefCheck) {
    if (fTreeStorage == nullptr) {
      NLogError("NBinningPoint::RecalculateStorageCoords: Storage tree is nullptr !!! Skipping check ...");
    }
    else {
      if (std::find(ids.begin(), ids.end(), fEntryNumber) == ids.end() && fEntryNumber >= 0 &&
          fEntryNumber < fTreeStorage->GetEntries()) {
        NLogError("NBinningPoint::RecalculateStorageCoords: Entry %lld not found in binning definition '%s' !!!",
                  fEntryNumber, fBinning->GetCurrentDefinitionName().c_str());

        // loop over all available definitions and print their ids
        NLogError("Available binning definitions for entry=%lld :", fEntryNumber);
        std::string firstDefName;
        for (const auto & kv : fBinning->GetDefinitions()) {
          NBinningDef * def = kv.second;
          if (def == nullptr) continue;
          std::vector<Long64_t> defIds = def->GetIds();
          if (std::find(defIds.begin(), defIds.end(), fEntryNumber) != defIds.end()) {
            if (firstDefName.empty()) firstDefName = kv.first;
            NLogError("  Definition '%s' size=%zu ", kv.first.c_str(), defIds.size());
          }
        }

        NLogError(
            "One can set definition via 'NBinning::SetCurrentDefinitionName(\"%s\")' before calling 'GetEntry(%lld)'",
            firstDefName.c_str(), fEntryNumber);
        Reset();
        return false;
      }
    }
  }

  // binDef->Print();
  for (Int_t i = 0; i < fNDimensions; ++i) {
    TAxis * axis        = fBinning->GetAxes()[i];
    TAxis * axisStorage = (TAxis *)binDef->GetAxes()->At(i);
    fBaseBinMin[i]      = axisRanges[i][1];
    fBaseBinMax[i]      = axisRanges[i][2];
    fMins[i]            = axis->GetBinLowEdge(axisRanges[i][1]);
    fMaxs[i]            = axis->GetBinUpEdge(axisRanges[i][2]);
    // FIXME: Check if GetBinLabel works when min and max are not the same
    fLabels[i]        = axis->GetBinLabel(axisRanges[i][1]);
    fStorageCoords[i] = axisStorage->FindBin((fMins[i] + fMaxs[i]) / 2.0);
  }

  return true;
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

std::string NBinningPoint::GetString(const std::string & prefix, bool all) const
{

  ///
  /// Returns point as string
  ///

  std::string title = !prefix.empty() ? prefix + " " : "";
  for (int i = 0; i < fNDimensions; i++) {
    TAxis * a = fBinning->GetAxes()[i];
    if (a == nullptr) {
      NLogError("NBinningPoint::GetTitle: Axis %d is nullptr !!!", i);
      continue;
    }

    // check type of axis
    if (fBinning->GetAxisType(i) == AxisType::kVariable || all) {
      if (a->IsAlphanumeric()) {
        title += TString::Format("%s[%s] ", a->GetName(), a->GetBinLabel(fBaseBinMin[i])).Data();
      }
      else {
        title += TString::Format("%s[%.3f,%.3f] ", a->GetName(), fMins[i], fMaxs[i]).Data();
      }
    }
  }
  if (title.back() == ' ') {
    title.pop_back(); // remove last space
  }
  return title;
}
Long64_t NBinningPoint::Fill(bool ignoreFilledCheck)
{
  ///
  /// Fill the content histogram at the current point
  ///
  if (fBinning == nullptr || fBinning->GetContent() == nullptr) {
    NLogError("NBinningPoint::Fill: Binning or content is nullptr");
    return -1;
  }

  // TODO: Find more efficient way to verify if bin exists
  Long64_t bin = fBinning->GetContent()->GetBin(fContentCoords, kFALSE);
  if (bin >= 0 && ignoreFilledCheck == false) {
    fBinning->GetDefinition()->GetContent()->SetBinContent(fStorageCoords, bin);
    fBinning->GetDefinition()->GetIds().push_back(bin);
    // NLogError("NBinningPoint::Fill: Bin for content already exists for coordinates: %s",
    //                NUtils::GetCoordsString(NUtils::ArrayToVector(fContentCoords, fContentNDimensions)).c_str());
    return -bin;
  }
  bin = fBinning->GetContent()->GetBin(fContentCoords, kTRUE);
  fBinning->GetContent()->SetBinContent(fContentCoords, 1);

  return bin;
}

bool NBinningPoint::SetPointContentFromLinearIndex(Long64_t linBin, bool checkBinningDef)
{
  ///
  /// Set point content coordinates from linear index
  ///
  if (fBinning == nullptr || fBinning->GetContent() == nullptr) {
    NLogError("NBinningPoint::SetPointContentFromLinearIndex: Binning or content is nullptr");
    return false;
  }

  if (linBin < 0 || linBin >= fBinning->GetContent()->GetNbins()) {
    NLogError("NBinningPoint::SetPointContentFromLinearIndex: Invalid linear bin index: %lld", linBin);
    return false;
  }

  fBinning->GetContent()->GetBinContent(linBin, fContentCoords);
  return RecalculateStorageCoords(linBin, checkBinningDef);
}

Double_t NBinningPoint::GetMin(std::string axis) const
{
  ///
  /// Get minimum value for specific axis
  ///

  // check if axis exists in fBinning->GetAxes()
  for (int i = 0; i < fNDimensions; i++) {
    TAxis * a = fBinning->GetAxes()[i];
    if (a == nullptr) {
      NLogError("NBinningPoint::GetMin: Axis %d is nullptr !!!", i);
      continue;
    }
    if (axis.compare(a->GetName()) == 0) {
      return fMins[i];
    }
  }

  NLogError("NBinningPoint::GetMin: Axis '%s' not found !!!", axis.c_str());
  return -1;
}

Double_t NBinningPoint::GetMax(std::string axis) const
{
  ///
  /// Get maximum value for specific axis
  ///

  // check if axis exists in fBinning->GetAxes()
  for (int i = 0; i < fNDimensions; i++) {
    TAxis * a = fBinning->GetAxes()[i];
    if (a == nullptr) {
      NLogError("NBinningPoint::GetMax: Axis %d is nullptr !!!", i);
      continue;
    }
    if (axis.compare(a->GetName()) == 0) {
      return fMaxs[i];
    }
  }

  NLogError("NBinningPoint::GetMax: Axis '%s' not found !!!", axis.c_str());
  return -1;
}

std::string NBinningPoint::GetLabel(std::string axis) const
{
  ///
  /// Get label for specific axis
  ///

  // check if axis exists in fBinning->GetAxes()
  for (int i = 0; i < fNDimensions; i++) {
    TAxis * a = fBinning->GetAxes()[i];
    if (a == nullptr) {
      NLogError("NBinningPoint::GetLabel: Axis %d is nullptr !!!", i);
      continue;
    }
    if (axis.compare(a->GetName()) == 0) {
      return fLabels[i];
    }
  }

  NLogError("NBinningPoint::GetLabel: Axis '%s' not found !!!", axis.c_str());
  return "";
}

} // namespace Ndmspc
