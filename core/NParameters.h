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

  /**
   * @brief Print the parameters.
   *
   * @param option Optional string to specify print options.
   */
  virtual void Print(Option_t * option = "") const override;

  /**
   * @brief Set the value and error of a parameter by bin index.
   *
   * @param bin Index of the parameter/bin.
   * @param value Value to set.
   * @param error Optional error value (default is 0).
   * @return true if the parameter was set successfully, false otherwise.
   */
  bool SetParameter(int bin, Double_t value, Double_t error = 0.);

  /**
   * @brief Set the value and error of a parameter by name.
   *
   * @param parName Name of the parameter.
   * @param value Value to set.
   * @param error Optional error value (default is 0).
   * @return true if the parameter was set successfully, false otherwise.
   */
  bool SetParameter(const char * parName, Double_t value, Double_t error = 0.);

  /**
   * @brief Get the value of a parameter by bin index.
   *
   * @param bin Index of the parameter/bin.
   * @return Value of the parameter.
   */
  Double_t GetParameter(int bin) const;

  /**
   * @brief Get the value of a parameter by name.
   *
   * @param parName Name of the parameter.
   * @return Value of the parameter.
   */
  Double_t GetParameter(const char * parName) const;

  /**
   * @brief Get the error of a parameter by bin index.
   *
   * @param bin Index of the parameter/bin.
   * @return Error of the parameter.
   */
  Double_t GetParError(int bin) const;

  /**
   * @brief Get the error of a parameter by name.
   *
   * @param parName Name of the parameter.
   * @return Error of the parameter.
   */
  Double_t GetParError(const char * parName) const;

  /**
   * @brief Returns the associated histogram.
   *
   * @return Pointer to the TH1D histogram object.
   */
  TH1D * GetHisto() const { return fHisto; }

  private:
  TH1D * fHisto{nullptr}; ///< Histogram with parameters

  /// \cond CLASSIMP
  ClassDefOverride(NParameters, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
