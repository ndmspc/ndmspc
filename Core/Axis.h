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
  Axis(TAxis * base = nullptr, int rebin = 1, int rebinShift = 0, int min = 1, int max = -1);
  virtual ~Axis();

  virtual void Print(Option_t * option = "", int spaces = 0) const;
  void         SetBaseAxis(TAxis * base) { fBaseAxis = base; }
  TAxis *      SetBaseAxis() const { return fBaseAxis; }

  void SetRebin(int rebin) { fRebin = rebin; }
  void SetRebinShift(int rebinShift) { fRebinStart = rebinShift + 1; }
  void SetBinMin(int min) { fBinMin = min; }
  void SetBinMax(int max) { fBinMax = max; }
  void SetRange(int min, int max) { fBinMin = min, fBinMax = max; }
  // void SetNBins(int nBins) { fNBins = nBins; }

  int GetRebin() const { return fRebin; }
  int GetRebinStart() const { return fRebinStart; }
  int GetRebinShift() const { return fRebinStart - 1; }
  int GetBinMin() const { return fBinMin; }
  int GetBinMax() const { return fBinMax; }
  int GetBinMinBase() const;
  int GetBinMaxBase() const;
  int GetNBins() const { return fNBins; }

  void   AddChild(Axis * axis) { fChildren.push_back(axis); }
  Axis * AddChild(int rebin /*= 1*/, int rebinShift /*= 0*/, int min /*= 1*/, int max /*= -1*/, Option_t * option = "");
  Axis * GetChild(int i) { return fChildren[i]; }

  Axis * AddRange(int rebin, int nBins = -1);

  void FillAxis(TAxis * axis);

  bool IsRangeValid();

  private:
  TAxis *             fBaseAxis{nullptr}; // base axis
  int                 fNBins{0};          // total number of bins
  int                 fRebin{1};          // rebin factor
  int                 fRebinStart{1};     // rebin start
  int                 fBinMin{1};         // range minimum
  int                 fBinMax{-1};        // range maximum
  std::vector<Axis *> fChildren;          // list of children axis
  /// \cond CLASSIMP
  ClassDef(Axis, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
