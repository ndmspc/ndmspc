#ifndef Ndmspc_NSystematicsStats_H
#define Ndmspc_NSystematicsStats_H

#include <TNamed.h>
#include <TH1D.h>

#include <vector>

namespace Ndmspc {

///
/// \class NSystematicsStats
///
/// \brief Statistics helper for measurements with optional uncertainties and reference value.
///
class NSystematicsStats : public TNamed {
  public:
  NSystematicsStats();
  NSystematicsStats(const char * name, const char * title = "Systematics stats", int nBins = 100, Double_t min = 0.0,
                    Double_t max = 1.0);
  virtual ~NSystematicsStats() = default;

  void AddMeasurement(Double_t value, Double_t error);
  void SetReference(Double_t value, Double_t error, bool fillHisto = false);

  void Compute();
  void Reset(Option_t * option = "");

  Double_t GetMean(Bool_t useWeights = kTRUE) const;
  Double_t GetStdDev(Bool_t useWeights = kTRUE) const;
  Double_t GetMeanStdError(Bool_t useWeights = kTRUE) const;

  Int_t    GetValidSampleCount() const;
  Double_t GetMeanAbsDeviationFromReference() const;
  Double_t GetMaxAbsDeviationFromReference() const;
  Double_t GetMeanRelDeviationFromReference() const;
  Double_t GetMaxRelDeviationFromReference() const;

  TH1D * GetHisto() const { return fHisto; }
  TH1D * GetDevHisto() const { return fDevHisto; }
  TH1D * GetRelDevHisto() const { return fRelDevHisto; }

  private:
  std::vector<Double_t> fValues;         ///< Added measurement values
  std::vector<Double_t> fErrors;         ///< Added measurement errors (same size as values)
  TH1D *                fHisto{nullptr}; ///< Histogram of added measurements (used for computing statistics)
  TH1D * fDevHisto{nullptr}; ///< Histogram of deviations from reference (used for computing statistics)
  TH1D * fRelDevHisto{nullptr}; ///< Histogram of relative deviations from reference (used for computing statistics)

  Bool_t   fHasReference{kFALSE};
  Double_t fReferenceValue{0.0};
  Double_t fReferenceError{0.0};

  Bool_t fDirty{kTRUE};

  Int_t fValidSampleCount{0};

  Double_t fWeightedMean{0.0};
  Double_t fWeightedStdDev{0.0};
  Double_t fWeightedMeanStdError{0.0};

  Double_t fUnweightedMean{0.0};
  Double_t fUnweightedStdDev{0.0};
  Double_t fUnweightedMeanStdError{0.0};

  Double_t fMeanAbsDeviationFromReference{0.0};
  Double_t fMaxAbsDeviationFromReference{0.0};
  Double_t fMeanRelDeviationFromReference{0.0};
  Double_t fMaxRelDeviationFromReference{0.0};

  void EnsureComputed() const;

  /// \cond CLASSIMP
  ClassDefOverride(NSystematicsStats, 1);
  /// \endcond
};

} // namespace Ndmspc

#endif