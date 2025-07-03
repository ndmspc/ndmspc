#ifndef Ndmspc_AnalysisFunctions_H
#define Ndmspc_AnalysisFunctions_H
#include <TObject.h>
#include <TMath.h>
#include <TF1.h>
namespace Ndmspc {

///
/// \class AnalysisFunctions
///
/// \brief AnalysisFunctions object
///	\author Martin Vala <mvala@cern.ch>
///

class AnalysisFunctions : public TObject {
  public:
  // AnalysisFunctions();
  // virtual ~AnalysisFunctions();

  static Double_t Pol1(double * x, double * par);
  static TF1 *    Pol1(const char * name, double xmin, double xmax);
  static Double_t Pol2(double * x, double * par);
  static TF1 *    Pol2(const char * name, double xmin, double xmax);
  static Double_t BreitWigner(double * x, double * par);
  static TF1 *    BreitWigner(const char * name, double xmin, double xmax);
  static Double_t Voigt(double * x, double * par);
  static TF1 *    Voigt(const char * name, double xmin, double xmax);
  static Double_t VoigtPol1(double * x, double * par);
  static TF1 *    VoigtPol1(const char * name, double xmin, double xmax);
  static Double_t VoigtPol2(double * x, double * par);
  static TF1 *    VoigtPol2(const char * name, double xmin, double xmax);
  static Double_t GausPol1(double * x, double * par);
  static TF1 *    GausPol1(const char * name, double xmin, double xmax);
  static Double_t GausPol2(double * x, double * par);
  static TF1 *    GausPol2(const char * name, double xmin, double xmax);

  /// \cond CLASSIMP
  ClassDef(AnalysisFunctions, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
