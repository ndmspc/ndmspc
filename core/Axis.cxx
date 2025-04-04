#include <TString.h>
#include <TSystem.h>
#include "Axis.h"
#include "Logger.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Axis);
/// \endcond

namespace Ndmspc {
Axis::Axis() : TObject()
{
  ///
  /// Default constructor
  ///
}
Axis::Axis(TAxis * base, int rebin, int rebinShift, int min, int max)
    : TObject(), fBaseAxis(base), fRebin(rebin), fRebinStart(rebinShift + 1), fBinMin(min), fBinMax(max)
{
  ///
  /// Constructor
  ///
  fNBins = (fBaseAxis->GetNbins() - rebinShift) / fRebin;

  // if ((fBaseAxis->GetNbins() % fRebin) - rebinShift != 0) fNBins--;

  if (max < min) {
    fBinMax = fNBins;
  }
};
Axis::~Axis()
{
  ///
  /// Destructor
  ///
}
void Axis::Print(Option_t * option, int spaces) const
{
  ///
  /// Print axis info
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  if (fBaseAxis == nullptr) {
    logger->Error("Base Axis is not set !!!");
    return;
  }

  logger->Info("%*cname=%s nbins=%d rebin=%d rebinShift=%d step=%.2E range=[%d,%d] rangeBase=[%d,%d]...", spaces, ' ',
               fBaseAxis->GetName(), fNBins, fRebin, fRebinStart - 1, (double)fRebin * fBaseAxis->GetBinWidth(1),
               fBinMin, fBinMax, GetBinMinBase(), GetBinMaxBase());
  TString opt(option);
  if (opt.Contains("baseOnly")) {
    return;
  }
  if (opt.Contains("ranges") && fChildren.size() == 0) {
    for (int iBin = fBinMin - 1; iBin < fBinMax; iBin++) {
      int binMin = iBin * fRebin + fRebinStart;
      int binMax = (binMin + fRebin) - 1;
      logger->Info("%*cbin=%d [%.2f,%.2f]", spaces + 2, ' ', iBin + 1, fBaseAxis->GetBinLowEdge(binMin),
                   fBaseAxis->GetBinUpEdge(binMax));
    }
  }
  for (auto child : fChildren) {
    child->Print(option, spaces + 2);
  }
}
Axis * Axis::AddChild(int rebin, int rebinShift, int min, int max, Option_t * option)
{
  ///
  /// Add child via parameters
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  if (max == -1) max = fBinMax;
  Axis * axis = new Axis(fBaseAxis, rebin, rebinShift, min, max);
  if (axis->GetBinMaxBase() > fBinMax) {
    logger->Error("Error: Out of range of in rangeBase: binMax=%d > baseBinMax=%d", axis->GetBinMaxBase(), fBinMax);
    logger->Error("  Base axis:");
    Print("baseOnly", 4);
    logger->Error("  New axis :");
    axis->Print("", 4);

    gSystem->Exit(1);
    return nullptr;
  }
  TString opt(option);
  if (!opt.IsNull()) {
    axis->Print(option);
  }
  fChildren.push_back(axis);
  return axis;
}

Axis * Axis::AddRange(int rebin, int nBins)
{
  ///
  /// Add Range from rebin and nbins
  ///

  auto logger = Ndmspc::Logger::getInstance("");
  if (fChildren.size() == 0) {
    Printf("Axis:");
    logger->Debug("Adding range rebin=%d nBins=%d ...", rebin, nBins);
    int rebinShift  = (fBinMin % rebin) - 1;
    int minFromBase = (fBinMin / rebin) + 1;
    SetRebinShift(rebinShift);
    SetBinMin(minFromBase + 1);
    SetBinMax(fBinMax - (rebinShift - 1));
    return AddChild(rebin, rebinShift, minFromBase, nBins, "ranges");
  }
  Printf("Adding range rebin=%d nBins=%d ...", rebin, nBins);
  // auto child = GetChild(fChildren.size() - 1);

  int nBinsBase = 0;
  for (auto c : fChildren) {
    nBinsBase += c->GetRebin() * (c->GetBinMax() - c->GetBinMin() + 1);
  }

  int rebinShift = nBinsBase % rebin;
  int min        = ((nBinsBase - rebinShift) / rebin) + 1;
  int max        = min + nBins - 1;
  if (GetRebinShift() > 0) {
    rebinShift += GetRebinShift();
  }

  return AddChild(rebin, rebinShift, min, max, "ranges");
}

bool Axis::IsRangeValid()
{
  ///
  /// Check if bin ranges are valid
  ///
  auto logger = Ndmspc::Logger::getInstance("");
  logger->Info("Checking ranage validity ...");

  if (fChildren.size() == 0) {
    logger->Error("Axis:");
    Print("", 0);
    logger->Error("Error: No ranges defined !!!");
    return false;
  }

  int bin = -1;
  for (auto child : fChildren) {
    // child->Print("ranges");
    int binFirst = child->GetBinMinBase() + fBinMin;
    int binLast  = child->GetBinMaxBase() + fBinMin;
    if (bin == -1) {
      bin = binLast + 1;
      // Printf("Ok");
      continue;
    }

    if (bin == binFirst) {
      bin = binLast + 1;
      // Printf("Ok");
      continue;
    }
    else {
      logger->Error("Axis:");
      child->Print("ranges");
      logger->Error("Invalid bin range: %d != %d in axis below", bin, binFirst);
      return false;
    }
  }

  return true;
}
void Axis::FillAxis(TAxis * axis)
{
  //
  // Fill Axis with current binning
  //
  double bins[fNBins + 1];
  int    count = 0;
  for (auto child : fChildren) {
    // child->Print();
    if (count == 0) {
      bins[count++] = fBaseAxis->GetBinLowEdge(child->GetBinMinBase());
    }

    for (int iBin = child->GetBinMin(); iBin <= child->GetBinMax(); iBin++) {
      bins[count++] = fBaseAxis->GetBinUpEdge(iBin * child->GetRebin() + child->GetRebinShift());
    }
  }
  if (count == 0) {
    return;
  }
  axis->Set(count - 1, bins);
}
int Axis::GetBinMinBase() const
{
  if (fRebin == 1) return fBinMin;

  return ((fBinMin - 1) * fRebin) + 1 + GetRebinShift();
}
int Axis::GetBinMaxBase() const
{
  if (fRebin == 1) return fBinMax;
  return fBinMax * fRebin + GetRebinShift();
}

} // namespace Ndmspc
