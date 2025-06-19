#include <TString.h>
#include "NBinning.h"
#include "NHnSparseTree.h"
#include "NLogger.h"
#include "NHnSparseTreePoint.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHnSparseTreePoint);
/// \endcond

namespace Ndmspc {
NHnSparseTreePoint::NHnSparseTreePoint(NHnSparseTree * hnst) : TObject(), fHnst(hnst)
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
  NLogger::Trace("Setting point content: %s", NUtils::GetCoordsString(content, -1).c_str());
  //
  if (content.size() != fPointContent.size()) {
    NLogger::Error("Content size does not match point size !!!");
    return;
  }
  fPointContent = content;

  std::vector<std::vector<int>> axisRanges = fHnst->GetBinning()->GetAxisRanges(content);
  std::vector<int>              coords(fHnst->GetNdimensions(), 1);
  // print axis ranges
  // NLogger::Debug("Setting point storage coordinates:");
  // NLogger::Debug("  %s", NUtils::GetCoordsString(coords, -1).c_str());
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
//     NLogger::Error("Storage size does not match point size !!!");
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
  NLogger::Info("NHnSparseTreePoint: opt='%s'", opt.Data());

  // Print content point coordinates
  NLogger::Info("  content: %s", NUtils::GetCoordsString(fPointContent, -1).c_str());
  NLogger::Info("  storage: %s", NUtils::GetCoordsString(fPointStorage, -1).c_str());
  std::vector<std::vector<int>> axisRanges = fHnst->GetBinning()->GetAxisRanges(fPointContent);

  if (opt.Contains("A")) {
    NLogger::Info("  Axis ranges:");
    for (size_t i = 0; i < axisRanges.size(); i++) {
      std::string rangeStr = NUtils::GetCoordsString(axisRanges[i], -1);
      TAxis *     axis     = fHnst->GetBinning()->GetAxes()[i];

      double_t min = axis->GetBinLowEdge(axisRanges[i][1]);
      double_t max = axis->GetBinUpEdge(axisRanges[i][2]);
      int      bin = fHnst->GetAxis(i)->FindBin((min + max) / 2.0);
      NLogger::Info("    [%d] name='%s' title='%s' range=[%.3f,%.3f] localBin=%d baseRange[%d,%d]", i, axis->GetName(),
                    axis->GetTitle(), min, max, bin, axisRanges[i][1], axisRanges[i][2]);
    }
  }
}

std::vector<int> NHnSparseTreePoint::GetPointBinning(Int_t axisId) const
{
  ///
  /// Returns point binning for given axis
  ///

  if (axisId < 0 || axisId >= fHnst->GetNdimensions()) {
    NLogger::Error("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
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
    NLogger::Error("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
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
    NLogger::Error("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
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
    NLogger::Error("Axis index %d is out of range [0, %d)", axisId, fHnst->GetNdimensions());
    return 0.0;
  }
  return fPointMax[axisId];
}

} // namespace Ndmspc
