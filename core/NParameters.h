#ifndef Ndmspc_NParameters_H
#define Ndmspc_NParameters_H
#include <TH1D.h>
#include <cstddef>
#include "Rtypes.h"

namespace Ndmspc {

///
/// \class NParameters
///
/// \brief NParameters object
///	\author Martin Vala <mvala@cern.ch>
///
class NParameters : public TNamed {
  public:
  NParameters(const char * name = "parameters", const char * title = "Parameters",
              std::vector<std::string> paramNames = {});
  virtual ~NParameters();

  virtual void Print(Option_t * option = "") const override;

  bool SetParameter(int bin, Double_t value, Double_t error = 0.);
  bool SetParameter(const char * parName, Double_t value, Double_t error = 0.);

  Double_t GetParameter(int bin) const;
  Double_t GetParameter(const char * parName) const;
  Double_t GetParError(int bin) const;
  Double_t GetParError(const char * parName) const;

  TH1D * GetHisto() const { return fHisto; }

  private:
  TH1D * fHisto{nullptr}; ///< Histogram with parameters

  /// \cond CLASSIMP
  ClassDefOverride(NParameters, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
