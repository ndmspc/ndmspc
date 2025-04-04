#ifndef NdmspcAxis_H
#define NdmspcAxis_H
#include <TObject.h>
#include <TAxis.h>
#include <vector>

namespace Ndmspc {

///
/// \class Axis
///
/// \brief Axis object
///	\author Martin Vala <mvala@cern.ch>
///

class Axis : public TObject {
  public:
  Axis();
  Axis(TAxis * base, int rebin = 1, int rebinShift = 0, int min = 1, int max = -1);
  virtual ~Axis();

  /// Print function
  virtual void Print(Option_t * option = "") const { Print(option, 0); }
  virtual void Print(Option_t * option, int spaces) const;

  /// Sets base axis
  void SetBaseAxis(TAxis * base) { fBaseAxis = base; }

  /// Sets rebin
  void SetRebin(int rebin) { fRebin = rebin; }
  /// Sets rebin shift
  void SetRebinShift(int rebinShift) { fRebinStart = rebinShift + 1; }
  /// Sets bin minimum
  void SetBinMin(int min) { fBinMin = min; }
  /// Sets bin maximum
  void SetBinMax(int max) { fBinMax = max; }
  /// Sets range (minimum and maximum)
  void SetRange(int min, int max) { fBinMin = min, fBinMax = max; }
  // void SetNBins(int nBins) { fNBins = nBins; }

  /// Get base axixs
  TAxis * GetBaseAxis() const { return fBaseAxis; }
  /// Returns rebin
  int GetRebin() const { return fRebin; }
  /// Returns rebin start
  int GetRebinStart() const { return fRebinStart; }
  /// Returns rebin shift
  int GetRebinShift() const { return fRebinStart - 1; }
  /// Returns bin minimum
  int GetBinMin() const { return fBinMin; }
  /// Return bin maximum
  int GetBinMax() const { return fBinMax; }
  /// Returns base minimum
  int GetBinMinBase() const;
  /// Returns base maximum
  int GetBinMaxBase() const;
  /// Returns number of bins
  int GetNBins() const { return fNBins; }

  /// Add axis child
  void AddChild(Axis * axis) { fChildren.push_back(axis); }
  /// Add axis child via parameters
  Axis * AddChild(int rebin /*= 1*/, int rebinShift /*= 0*/, int min /*= 1*/, int max /*= -1*/, Option_t * option = "");
  /// Returns child
  Axis * GetChild(int i) { return fChildren[i]; }

  /// Add range
  Axis * AddRange(int rebin, int nBins = -1);

  /// Fill axis
  void FillAxis(TAxis * axis);

  /// Checks if range is valid
  bool IsRangeValid();

  private:
  TAxis *             fBaseAxis = {nullptr}; ///< Base axis
  int                 fNBins{0};             ///< Total number of bins
  int                 fRebin{1};             ///< rebin factor
  int                 fRebinStart{1};        ///< rebin start
  int                 fBinMin{1};            ///< range minimum
  int                 fBinMax{-1};           ///< range maximum
  std::vector<Axis *> fChildren;             ///< list of children axis

  /// \cond CLASSIMP
  ClassDef(Axis, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
