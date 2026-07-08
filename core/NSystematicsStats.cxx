

#include <cmath>
#include <limits>
#include <TMath.h>
#include "NLogger.h"
#include "NSystematicsStats.h"
/// \cond CLASSIMP
ClassImp(Ndmspc::NSystematicsStats);
/// \endcond

namespace Ndmspc {

namespace {
Double_t NaN()
{
  return std::numeric_limits<Double_t>::quiet_NaN();
}

Bool_t IsFinite(Double_t x)
{
  return std::isfinite(x);
}
} // namespace

NSystematicsStats::NSystematicsStats() : TNamed("systematicsStats", "Systematics stats") {}

NSystematicsStats::NSystematicsStats(const char * name, const char * title, int nBins, Double_t min, Double_t max)
    : TNamed(name, title)
{
  fHisto       = new TH1D(Form("%s_histo", name), title, nBins, min, max);
  fDevHisto    = new TH1D(Form("%s_devHisto", name), Form("Dev %s", title), nBins * 10, min / 2, max / 2);
  fRelDevHisto = new TH1D(Form("%s_relDevHisto", name), Form("RelDev %s", title), 201, -1.005, 1.005);
}

void NSystematicsStats::Reset(Option_t * option)
{
  fValues.clear();
  fErrors.clear();
  fHisto->Reset();
  fDevHisto->Reset();
  fRelDevHisto->Reset();
  fHasReference   = kFALSE;
  fReferenceValue = 0.0;
  fReferenceError = 0.0;
  fDirty          = kTRUE;

  TNamed::Clear(option);
}

void NSystematicsStats::Print(Option_t * /*option*/) const
{
  EnsureComputed();
  NLogInfo("NSystematicsStats: %s", GetName());
  NLogInfo("  Valid sample count: %d", fValidSampleCount);
  NLogInfo("  Weighted mean: %e +/- %e", fWeightedMean, fWeightedMeanStdError);
  NLogInfo("  Weighted std dev: %e", fWeightedStdDev);
  NLogInfo("  Unweighted mean: %e +/- %e", fUnweightedMean, fUnweightedMeanStdError);
  NLogInfo("  Unweighted std dev: %e", fUnweightedStdDev);

  if (fHasReference) {
    
    NLogInfo("  Mean abs deviation from reference: %e", fMeanAbsDeviationFromReference);
    NLogInfo("  Max abs deviation from reference: %e", fMaxAbsDeviationFromReference);
    NLogInfo("  Mean rel deviation from reference: %e", fMeanRelDeviationFromReference);
    NLogInfo("  Max rel deviation from reference: %e", fMaxRelDeviationFromReference);

    NLogInfo("    Reference value: \t%e +/- %e", fReferenceValue, fReferenceError);
  }

  // print all values and errors
  for (size_t i = 0; i < fValues.size(); ++i) {
    NLogInfo("    Measurement %zu: \t%e +/- %e", i, fValues[i], fErrors[i]);
  }

}

void NSystematicsStats::AddMeasurement(Double_t value, Double_t error)
{
  fValues.push_back(value);
  fErrors.push_back(error);
  fHisto->Fill(value);
  fDirty = kTRUE;
}

void NSystematicsStats::SetReference(Double_t value, Double_t error, bool fillHisto)
{
  fHasReference   = kTRUE;
  fReferenceValue = value;
  fReferenceError = error;
  if (fillHisto) fHisto->Fill(value);
  fDirty = kTRUE;
}

void NSystematicsStats::Compute()
{
  fValidSampleCount = 0;

  fWeightedMean         = NaN();
  fWeightedStdDev       = NaN();
  fWeightedMeanStdError = NaN();

  fUnweightedMean         = NaN();
  fUnweightedStdDev       = NaN();
  fUnweightedMeanStdError = NaN();

  // fMeanAbsDeviationFromReference = NaN();
  // fMaxAbsDeviationFromReference  = NaN();
  // fMeanRelDeviationFromReference = NaN();
  // fMaxRelDeviationFromReference  = NaN();

  fMeanAbsDeviationFromReference = 0.0;
  fMaxAbsDeviationFromReference  = 0.0;
  fMeanRelDeviationFromReference = 0.0;
  fMaxRelDeviationFromReference  = 0.0;

  Double_t sumW  = 0.0;
  Double_t sumWX = 0.0;

  Double_t sumX = 0.0;
  Int_t    nUnw = 0;

  for (size_t i = 0; i < fValues.size(); ++i) {
    const Double_t value = fValues[i];
    const Double_t error = fErrors[i];

    if (!IsFinite(value)) {
      continue;
    }

    fValidSampleCount++;

    sumX += value;
    nUnw++;

    if (!IsFinite(error) || error <= 0.0) {
      continue;
    }

    const Double_t weight = 1.0 / (error * error);
    sumW += weight;
    sumWX += weight * value;
  }

  if (fHasReference && IsFinite(fReferenceValue)) {
    sumX += fReferenceValue;
    nUnw++;

    if (IsFinite(fReferenceError) && fReferenceError > 0.0) {
      const Double_t weightRef = 1.0 / (fReferenceError * fReferenceError);
      sumW += weightRef;
      sumWX += weightRef * fReferenceValue;
    }
  }

  if (nUnw > 0) {
    fUnweightedMean = sumX / static_cast<Double_t>(nUnw);

    Double_t sumSq = 0.0;
    for (size_t i = 0; i < fValues.size(); ++i) {
      const Double_t value = fValues[i];
      if (!IsFinite(value)) {
        continue;
      }
      const Double_t diff = value - fUnweightedMean;
      sumSq += diff * diff;
    }
    if (fHasReference && IsFinite(fReferenceValue)) {
      const Double_t diff = fReferenceValue - fUnweightedMean;
      sumSq += diff * diff;
    }

    fUnweightedStdDev       = TMath::Sqrt(sumSq / static_cast<Double_t>(nUnw));
    fUnweightedMeanStdError = fUnweightedStdDev / TMath::Sqrt(static_cast<Double_t>(nUnw));
  }

  if (sumW > 0.0) {
    fWeightedMean = sumWX / sumW;

    Double_t sumWDiff = 0.0;
    for (size_t i = 0; i < fValues.size(); ++i) {
      const Double_t value = fValues[i];
      const Double_t error = fErrors[i];

      if (!IsFinite(value) || !IsFinite(error) || error <= 0.0) {
        continue;
      }

      const Double_t weight = 1.0 / (error * error);
      const Double_t diff   = value - fWeightedMean;
      sumWDiff += weight * diff * diff;
    }

    if (fHasReference && IsFinite(fReferenceValue) && IsFinite(fReferenceError) && fReferenceError > 0.0) {
      const Double_t weightRef = 1.0 / (fReferenceError * fReferenceError);
      const Double_t diffRef   = fReferenceValue - fWeightedMean;
      sumWDiff += weightRef * diffRef * diffRef;
    }

    fWeightedStdDev       = TMath::Sqrt(sumWDiff / sumW);
    fWeightedMeanStdError = TMath::Sqrt(1.0 / sumW);
  }

  //   NLogDebug ("NSystematicsStats::Compute: Weighted mean = %f, Weighted std dev = %f, Weighted mean std error = %f",
  //            fWeightedMean, fWeightedStdDev, fWeightedMeanStdError);
  //   NLogDebug("NSystematicsStats::Compute: Unweighted mean = %f, Unweighted std dev = %f, Unweighted mean std error =
  //   %f",
  //            fUnweightedMean, fUnweightedStdDev, fUnweightedMeanStdError);
  // print fHasReference
  // NLogDebug("NSystematicsStats::Compute: Has reference = %s, Reference value = %f, Reference error = %f",
  //           fHasReference ? "true" : "false", fReferenceValue, fReferenceError);

  if (fHasReference && IsFinite(fReferenceValue)) {
    fDevHisto->Reset();
    fRelDevHisto->Reset();
    Int_t          nDev      = 0;
    Double_t       sumAbsDev = 0.0;
    Double_t       maxAbsDev = 0.0;
    Double_t       sumRelDev = 0.0;
    Double_t       maxRelDev = 0.0;
    const Double_t absRef    = TMath::Abs(fReferenceValue);
    const Bool_t   refZero   = absRef <= std::numeric_limits<Double_t>::epsilon();

    for (size_t i = 0; i < fValues.size(); ++i) {
      const Double_t value = fValues[i];
      if (!IsFinite(value)) {
        continue;
      }

      const Double_t dev = value - fReferenceValue;
      fDevHisto->Fill(dev);
      const Double_t absDev = TMath::Abs(dev);
      sumAbsDev += absDev;
      if (absDev > maxAbsDev) {
        maxAbsDev = absDev;
      }

      if (!refZero) {
        const Double_t relAbsDev = absDev / absRef;
        fRelDevHisto->Fill(dev / absRef);
        sumRelDev += relAbsDev;
        if (relAbsDev > maxRelDev) {
          maxRelDev = relAbsDev;
        }
      }

      nDev++;
    }

    if (nDev > 0) {
      fMeanAbsDeviationFromReference = sumAbsDev / static_cast<Double_t>(nDev);
      fMaxAbsDeviationFromReference  = maxAbsDev;
      // NLogDebug(
      //     "NSystematicsStats::Compute: Mean abs deviation from reference = %f, Max abs deviation from reference = %f",
      //     fMeanAbsDeviationFromReference, fMaxAbsDeviationFromReference);

      if (!refZero) {
        fMeanRelDeviationFromReference = sumRelDev / static_cast<Double_t>(nDev);
        fMaxRelDeviationFromReference  = maxRelDev;
      }
    }
  }

  fDirty = kFALSE;
}

void NSystematicsStats::EnsureComputed() const
{
  if (fDirty) {
    const_cast<NSystematicsStats *>(this)->Compute();
  }
}

Double_t NSystematicsStats::GetMean(Bool_t useWeights) const
{
  EnsureComputed();
  return useWeights ? fWeightedMean : fUnweightedMean;
}

Double_t NSystematicsStats::GetStdDev(Bool_t useWeights) const
{
  EnsureComputed();
  return useWeights ? fWeightedStdDev : fUnweightedStdDev;
}

Double_t NSystematicsStats::GetMeanStdError(Bool_t useWeights) const
{
  EnsureComputed();
  return useWeights ? fWeightedMeanStdError : fUnweightedMeanStdError;
}

Int_t NSystematicsStats::GetValidSampleCount() const
{
  EnsureComputed();
  return fValidSampleCount;
}

Double_t NSystematicsStats::GetMeanAbsDeviationFromReference() const
{
  EnsureComputed();
  return fMeanAbsDeviationFromReference;
}

Double_t NSystematicsStats::GetMaxAbsDeviationFromReference() const
{
  EnsureComputed();
  return fMaxAbsDeviationFromReference;
}

Double_t NSystematicsStats::GetMeanRelDeviationFromReference() const
{
  EnsureComputed();
  return fMeanRelDeviationFromReference;
}

Double_t NSystematicsStats::GetMaxRelDeviationFromReference() const
{
  EnsureComputed();
  return fMaxRelDeviationFromReference;
}

} // namespace Ndmspc