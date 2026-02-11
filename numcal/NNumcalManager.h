#ifndef Ndmspc_NNumcalManager_H
#define Ndmspc_NNumcalManager_H
#include <TNamed.h>
#include <functional>
#include <string>
#include <vector>

namespace Ndmspc {

///
/// \class NNumcalManager
///
/// \brief NNumcalManager object
///	\author Martin Vala <mvala@cern.ch>
///

class NNumcalManager : public TNamed {
  public:
  NNumcalManager(const char *name = "", const char *title = "");
  virtual ~NNumcalManager();

  using Integrand = std::function<double(const std::vector<double>&)>;

  struct VegasOptions {
    int ndim = 1;
    double epsrel = 1e-3;
    double epsabs = 1e-12;
    int seed = 0;
    int mineval = 0;
    int maxeval = 50000;
    int nstart = 1000;
    int nincrease = 500;
    int nbatch = 1000;
    int gridno = 0;
    int verbose = 0;
  };

  struct SuaveOptions {
    int ndim = 1;
    double epsrel = 1e-3;
    double epsabs = 1e-12;
    int seed = 0;
    int mineval = 0;
    int maxeval = 50000;
    int nnew = 1000;
    int nmin = 2;
    double flatness = 25.0;
    int verbose = 0;
  };

  struct DivonneOptions {
    int ndim = 1;
    double epsrel = 1e-3;
    double epsabs = 1e-12;
    int seed = 0;
    int mineval = 0;
    int maxeval = 50000;
    int key1 = 47;
    int key2 = 1;
    int key3 = 1;
    int maxpass = 5;
    double border = 0.0;
    double maxchisq = 10.0;
    double mindeviation = 0.25;
    int verbose = 0;
  };

  struct CuhreOptions {
    int ndim = 1;
    double epsrel = 1e-3;
    double epsabs = 1e-12;
    int mineval = 0;
    int maxeval = 50000;
    int key = 0;
    int verbose = 0;
  };

  struct VegasResult {
    int neval = 0;
    int fail = 0;
    std::vector<double> integral;
    std::vector<double> error;
    std::vector<double> prob;
  };

  void ClearFunctions();
  void AddFunction(const Integrand& fn, const std::string& label = "");
  void SetFunctions(const std::vector<Integrand>& fns, const std::vector<std::string>& labels = {});

  VegasResult RunVegas(const VegasOptions& options) const;
  VegasResult RunSuave(const SuaveOptions& options) const;
  VegasResult RunDivonne(const DivonneOptions& options) const;
  VegasResult RunCuhre(const CuhreOptions& options) const;

  // Simple example: integral of f(x,y)=x*y over unit square
  static VegasResult ExampleVegas();
  static VegasResult ExampleSuave();
  static VegasResult ExampleDivonne();
  static VegasResult ExampleCuhre();

  /// \cond CLASSIMP
  ClassDef(NNumcalManager, 1);
  /// \endcond;
  private:
  static int VegasIntegrand(const int* ndim, const double x[], const int* ncomp, double f[], void* userdata);
  std::vector<Integrand> fFunctions; //!
  std::vector<std::string> fLabels;  //!
};
} // namespace Ndmspc
#endif
