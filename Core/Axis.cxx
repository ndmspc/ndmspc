#include <TString.h>
#include <TSystem.h>
#include "Axis.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Axis);
/// \endcond

namespace Ndmspc {
Axis::Axis(TAxis * base, int rebin, int rebinShift, int min, int max)
    : TObject(), fBaseAxis(base), fRebin(rebin), fRebinStart(rebinShift + 1), fBinMin(min), fBinMax(max)
{
  fNBins = (fBaseAxis->GetNbins() - rebinShift) / fRebin;

  // if ((fBaseAxis->GetNbins() % fRebin) - rebinShift != 0) fNBins--;

  if (max < min) {
    fBinMax = fNBins;
  }
};
Axis::~Axis() {}
void Axis::Print(Option_t * option, int spaces) const
{
  if (fBaseAxis == nullptr) {
    Printf("Base Axis is not set !!!");
    return;
  }

  Printf("%*cname=%s nbins=%d rebin=%d rebinShift=%d step=%.2f range=[%d,%d] rangeBase=[%d,%d]...", spaces, ' ',
         fBaseAxis->GetName(), fNBins, fRebin, fRebinStart - 1, (double)fRebin * fBaseAxis->GetBinWidth(1), fBinMin,
         fBinMax, GetBinMinBase(), GetBinMaxBase());
  TString opt(option);
  if (opt.Contains("baseOnly")) {
    return;
  }
  if (opt.Contains("ranges") && fChildren.size() == 0) {
    for (int iBin = fBinMin - 1; iBin < fBinMax; iBin++) {
      int binMin = iBin * fRebin + fRebinStart;
      int binMax = (binMin + fRebin) - 1;
      Printf("%*cbin=%d [%.2f,%.2f]", spaces + 2, ' ', iBin + 1, fBaseAxis->GetBinLowEdge(binMin),
             fBaseAxis->GetBinUpEdge(binMax));
    }
  }
  for (auto child : fChildren) {
    child->Print(option, spaces + 2);
  }
}
Axis * Axis::AddChild(int rebin, int rebinShift, int min, int max, Option_t * option)
{
  if (max == -1) max = fBinMax;
  Axis * axis = new Axis(fBaseAxis, rebin, rebinShift, min, max);
  if (axis->GetBinMaxBase() > fBinMax) {
    Printf("Error: Out of range of in rangeBase: binMax=%d > baseBinMax=%d", axis->GetBinMaxBase(), fBinMax);
    Printf("  Base axis:");
    Print("baseOnly", 4);
    Printf("  New axis :");
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
  //
  // Add Range from rebin and nbins
  //

  if (fChildren.size() == 0) {
    Printf("Axis:");
    Print("baseOnly");
    Printf("Adding range rebin=%d nBins=%d ...", rebin, nBins);
    int rebinShift  = (fBinMin % rebin) - 1;
    int minFromBase = (fBinMin / rebin) + 1;
    SetRebinShift(rebinShift);
    SetBinMin(minFromBase + 1);
    SetBinMax(fBinMax - (rebinShift - 1));
    return AddChild(rebin, rebinShift, minFromBase, nBins, "ranges");
  }
  Printf("Adding range rebin=%d nBins=%d ...", rebin, nBins);
  auto child = GetChild(fChildren.size() - 1);

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
  //
  // Check if bin ranges are valid
  //

  Printf("Checking ranage validity ...");

  if (fChildren.size() == 0) {
    Printf("Axis:");
    Print();
    Printf("Error: No ranges defined !!!");
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
      Printf("Axis:");
      child->Print("ranges");
      Printf("Invalid bin range: %d != %d in axis below", bin, binFirst);
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
