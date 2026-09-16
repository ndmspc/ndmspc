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
  /// @brief Default constructor.
  NSystematicsStats();
  /**
   * @brief Constructor with histogram bounds.
   * @param name Object name.
   * @param title Object title (default: "Systematics stats").
   * @param nBins Number of histogram bins (default: 100).
   * @param min Histogram lower bound (default: 0.0).
   * @param max Histogram upper bound (default: 1.0).
   */
  NSystematicsStats(const char * name, const char * title = "Systematics stats", int nBins = 100, Double_t min = 0.0,
                    Double_t max = 1.0);
  virtual ~NSystematicsStats() = default;

  /**
   * @brief Add one measurement to the sample.
   * @param value Measured value.
   * @param error Measurement uncertainty on value.
   */
  void AddMeasurement(Double_t value, Double_t error);
  /**
   * @brief Set the reference value the deviations are computed against.
   *
   * When set, the reference is also included in the computed means and standard
   * deviations (with its own weight when the reference error is positive).
   *
   * @param value Reference value.
   * @param error Uncertainty on the reference value.
   * @param fillHisto When true, also fill the deviations histograms (default: false).
   */
  void SetReference(Double_t value, Double_t error, bool fillHisto = false);

  /// @brief Compute the means, standard deviations and deviations from the reference.
  void Compute();
  /**
   * @brief Reset the accumulated measurements and computed statistics.
   * @param option Optional ROOT option string (unused).
   */
  void Reset(Option_t * option = "");
  /**
   * @brief Print the computed statistics.
   * @param option Optional ROOT option string (unused).
   */
  virtual void Print(Option_t * option = "") const override;

  /**
   * @brief Get the mean of the measurements.
   * @param useWeights When true use inverse-variance weighting, otherwise an unweighted mean (default: true).
   * @return The mean value.
   */
  Double_t GetMean(Bool_t useWeights = kTRUE) const;
  /**
   * @brief Get the standard deviation of the measurements.
   * @param useWeights When true use the weighted standard deviation, otherwise the unweighted one (default: true).
   * @return The standard deviation.
   */
  Double_t GetStdDev(Bool_t useWeights = kTRUE) const;
  /**
   * @brief Get the standard error of the mean.
   *
   * Weighted mode returns the inverse-variance weighted error (sqrt(1/sum w)).
   * Unweighted mode returns the standard error of the sample scatter
   * (stddev/sqrt(N)). Measurements whose uncertainty is non-finite or not
   * positive are excluded from the weighted statistics (they are still counted
   * by GetValidSampleCount()).
   *
   * @param useWeights When true use the weighted error, otherwise the unweighted one (default: true).
   * @return The standard error of the mean.
   */
  Double_t GetMeanStdError(Bool_t useWeights = kTRUE) const;

  /// @brief Get the number of measurements with a finite value.
  Int_t    GetValidSampleCount() const;
  /// @brief Get the mean absolute deviation of the measurements from the reference value.
  Double_t GetMeanAbsDeviationFromReference() const;
  /// @brief Get the maximum absolute deviation of the measurements from the reference value.
  Double_t GetMaxAbsDeviationFromReference() const;
  /// @brief Get the mean relative deviation of the measurements from the reference value.
  Double_t GetMeanRelDeviationFromReference() const;
  /// @brief Get the maximum relative deviation of the measurements from the reference value.
  Double_t GetMaxRelDeviationFromReference() const;

  /// @brief Get the histogram of added measurements.
  TH1D * GetHisto() const { return fHisto; }
  /// @brief Get the histogram of deviations from the reference.
  TH1D * GetDevHisto() const { return fDevHisto; }
  /// @brief Get the histogram of relative deviations from the reference.
  TH1D * GetRelDevHisto() const { return fRelDevHisto; }

  private:
  std::vector<Double_t> fValues;         ///< Added measurement values
  std::vector<Double_t> fErrors;         ///< Added measurement errors (same size as values)
  TH1D *                fHisto{nullptr}; ///< Histogram of added measurements (used for computing statistics)
  TH1D * fDevHisto{nullptr}; ///< Histogram of deviations from reference (used for computing statistics)
  TH1D * fRelDevHisto{nullptr}; ///< Histogram of relative deviations from reference (used for computing statistics)

  Bool_t   fHasReference{kFALSE};    ///< Whether a reference value has been set
  Double_t fReferenceValue{0.0};     ///< Reference value deviations are computed against
  Double_t fReferenceError{0.0};     ///< Uncertainty on the reference value

  Bool_t fDirty{kTRUE}; ///< True when statistics must be recomputed before being read

  Int_t fValidSampleCount{0}; ///< Number of measurements with a finite value

  Double_t fWeightedMean{0.0};         ///< Inverse-variance weighted mean
  Double_t fWeightedStdDev{0.0};       ///< Weighted standard deviation
  Double_t fWeightedMeanStdError{0.0}; ///< Standard error of the weighted mean

  Double_t fUnweightedMean{0.0};         ///< Unweighted mean
  Double_t fUnweightedStdDev{0.0};       ///< Unweighted standard deviation
  Double_t fUnweightedMeanStdError{0.0}; ///< Standard error of the unweighted mean

  Double_t fMeanAbsDeviationFromReference{0.0}; ///< Mean absolute deviation from the reference value
  Double_t fMaxAbsDeviationFromReference{0.0};  ///< Maximum absolute deviation from the reference value
  Double_t fMeanRelDeviationFromReference{0.0}; ///< Mean relative deviation from the reference value
  Double_t fMaxRelDeviationFromReference{0.0};  ///< Maximum relative deviation from the reference value

  /// @brief Recompute the statistics if the cached results are stale.
  void EnsureComputed() const;

  /// \cond CLASSIMP
  ClassDefOverride(NSystematicsStats, 1);
  /// \endcond
};

} // namespace Ndmspc

#endif
