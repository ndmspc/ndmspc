#include <TString.h>
#include "NBinning.h"
#include "NHnSparseTree.h"
#include "NLogger.h"
#include "NHnSparseTreePoint.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseTreePoint);
/// \endcond

namespace Ndmspc {
NHnSparseTreePoint::NHnSparseTreePoint(NHnSparseTree * ngnt) : TObject(), fHnst(ngnt)
{
  ///
  /// Constructor
  ///

  Reset();
}
NHnSparseTreePoint::~NHnSparseTreePoint() {}

void NHnSparseTreePoint::Reset()
{
  ///
  /// Reset point content
  ///

  if (fHnst && fHnst->GetBinning()) {
    fPointContent.resize(fHnst->GetBinning()->GetContent()->GetNdimensions(), -1);
    fPointStorage.resize(fHnst->GetNdimensions(), -1);
    fPointMin.resize(fHnst->GetNdimensions(), 0.0);
    fPointMax.resize(fHnst->GetNdimensions(), 0.0);
    fPointLabel.resize(fHnst->GetNdimensions(), "");
  }
  else {
    fPointContent.clear();
    fPointStorage.clear();
    fPointMin.clear();
    fPointMax.clear();
    fPointLabel.clear();
  }
}

void NHnSparseTreePoint::SetPointContent(const std::vector<int> & content)
{
  ///
  /// Set point content
  ///

  //
  // Print content coordinates
  NLogTrace("Setting point content: %s", NUtils::GetCoordsString(content, -1).c_str());
  //
  if (content.size() != fPointContent.size()) {
    NLogError("Content size does not match point size %d != %d !!!", content.size(), fPointContent.size());
    return;
  }
  fPointContent = content;

  std::vector<std::vector<int>> axisRanges = fHnst->GetBinning()->GetAxisRanges(content);

  // for (size_t i = 0; i < axisRanges.size(); i++) {
  //   NLogTrace("Axis %zu: %s", i, NUtils::GetCoordsString(axisRanges[i], -1).c_str());
  // }

  std::vector<int> coords(fHnst->GetNdimensions(), 1);
  // print axis ranges
  // NLogDebug("Setting point storage coordinates:");
  // NLogDebug("  %s", NUtils::GetCoordsString(coords, -1).c_str());
  for (size_t i = 0; i < axisRanges.size(); i++) {
    std::string rangeStr = NUtils::GetCoordsString(axisRanges[i], -1);
    TAxis *     axis     = fHnst->GetBinning()->GetAxes()[i];

    double_t min     = axis->GetBinLowEdge(axisRanges[i][1]);
    double_t max     = axis->GetBinUpEdge(axisRanges[i][2]);
    int      bin     = fHnst->GetAxis(i)->FindBin((min + max) / 2.0);
    fPointStorage[i] = bin;
    fPointMin[i]     = min;
    fPointMax[i]     = max;
  }
}

// void NHnSparseTreePoint::SetPointStorage(const std::vector<int> & storage)
// {
//   ///
//   /// Set point storage
//   ///
//
//   if (storage.size() != fPointStorage.size()) {
//     NLogError("Storage size does not match point size !!!");
//     return;
//   }
//   fPointStorage = storage;
// }

void NHnSparseTreePoint::Print(Option_t * option) const
{
  ///
  /// Printing point information
  ///

  TString opt(option);
  opt.ToUpper();
  NLogInfo("NHnSparseTreePoint: opt='%s'", opt.Data());

  // Print content point coordinates
  NLogInfo("  content: %s", NUtils::GetCoordsString(fPointContent, -1).c_str());
  NLogInfo("  storage: %s", NUtils::GetCoordsString(fPointStorage, -1).c_str());
  std::vector<std::vector<int>> axisRanges = fHnst->GetBinning()->GetAxisRanges(fPointContent);

  if (opt.Contains("A")) {
    NLogInfo("  Axis ranges:");
    for (size_t i = 0; i < axisRanges.size(); i++) {
      std::string rangeStr = NUtils::GetCoordsString(axisRanges[i], -1);
      TAxis *     axis     = fHnst->GetBinning()->GetAxes()[i];

      double_t min = axis->GetBinLowEdge(axisRanges[i][1]);
      double_t max = axis->GetBinUpEdge(axisRanges[i][2]);
      int      bin = fHnst->GetAxis(i)->FindBin((min + max) / 2.0);
      NLogInfo("    [%d] [%c] name='%s' title='%s' range=[%.3f,%.3f] localBin=%d baseRange[%d,%d]", i,
                    fHnst->GetBinning()->GetAxisTypeChar(i), axis->GetName(), axis->GetTitle(), min, max, bin,
                    axisRanges[i][1], axisRanges[i][2]);
    }
  }
}

std::vector<int> NHnSparseTreePoint::GetPointBinning(Int_t axisId) const
{
  ///
  /// Returns point binning for given axis
  ///

  if (axisId < 0 || axisId >= fHnst->GetNdimensions()) {
    NLogError("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
    return {};
  }

  return fHnst->GetBinning()->GetAxisBinning(axisId, fPointContent);
}

std::string NHnSparseTreePoint::GetTitle(const std::string & prefix) const
{
  ///
  /// Returns point title
  ///

  std::string title = !prefix.empty() ? prefix + " " : "";
  for (int i = 0; i < fHnst->GetNdimensions(); i++) {
    TAxis * a = fHnst->GetAxis(i);

    if (a->GetNbins() > 1) {
      title += TString::Format("%s[%.3f,%.3f] ", a->GetName(), a->GetBinLowEdge(fPointStorage[i]),
                               a->GetBinUpEdge(fPointStorage[i]))
                   .Data();
    }
  }
  if (title.back() == ' ') {
    title.pop_back(); // remove last space
  }
  return title;
}
void NHnSparseTreePoint::GetPointMinMax(int axisId, double & min, double & max) const
{
  ///
  /// Fill min and max for given axis
  ///

  if (axisId < 0 || axisId >= fHnst->GetNdimensions()) {
    NLogError("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
    return;
  }
  TAxis * a = fHnst->GetAxis(axisId);

  min = a->GetBinLowEdge(fPointMin[axisId]);
  max = a->GetBinUpEdge(fPointMax[axisId]);
}
double NHnSparseTreePoint::GetPointMin(int axisId) const
{
  ///
  /// Returns point min for given axis
  ///

  if (axisId < 0 || axisId >= fHnst->GetNdimensions()) {
    NLogError("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
    return 0.0;
  }
  return fPointMin[axisId];
}

double NHnSparseTreePoint::GetPointMax(int axisId) const
{
  ///
  /// Returns point max for given axis
  ///

  if (axisId < 0 || axisId >= fHnst->GetNdimensions()) {
    NLogError("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
    return 0.0;
  }
  return fPointMax[axisId];
}

std::vector<int> NHnSparseTreePoint::GetVariableAxisIndexes() const
{
  ///
  /// Returns variable axis indexes
  ///

  if (fHnst && fHnst->GetBinning()) return fHnst->GetBinning()->GetAxisIndexes(AxisType::kVariable);

  return {};
}

Long64_t NHnSparseTreePoint::GetEntryNumber() const
{
  ///
  /// Returns entry number
  ///

  if (fHnst) {
    Int_t * p = new Int_t[fHnst->GetBinning()->GetContent()->GetNdimensions()];
    NUtils::VectorToArray(fPointContent, p);
    Long64_t entry = fHnst->GetBinning()->GetContent()->GetBin(p);
    delete[] p;
    return entry;
  }
  NLogError("NHnSparseTreePoint::GetEntryNumber: HnSparseTree is not set");
  return -1;
}
} // namespace Ndmspc
