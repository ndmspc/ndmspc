#include <TF1.h>
#include <TList.h>

TList * _currentFunctions = nullptr;

Double_t Pol1(double * x, double * par)
{
  return par[0] + x[0] * par[1];
}
Double_t Pol2(double * x, double * par)
{
  return par[0] + x[0] * par[1] + x[0] * x[0] * par[2];
}
Double_t VoigtPol2(double * x, double * par)
{
  return par[0] * TMath::Voigt(x[0] - par[1], par[3], par[2]) + Pol2(x, &par[4]);
}

Double_t GausPol2(double * x, double * par)
{
  return par[0] * TMath::Gaus(x[0], par[1], par[2]) + Pol2(x, &par[3]);
}

TList * RsnFunctions(std::string name, Double_t min, Double_t max, bool reuseFunctions = false)
{
  if (_currentFunctions && reuseFunctions) {
    return _currentFunctions;
  }
  if (_currentFunctions && !reuseFunctions) {
    delete _currentFunctions;
    _currentFunctions = nullptr;
  }
  _currentFunctions = new TList();

  if (name.compare("VoigtPol2") == 0) {
    TF1 * sigBgFnc = new TF1("VoigtPol2", VoigtPol2, min, max, 7);
    // set parameter names
    sigBgFnc->SetParNames("yield", "mass", "width", "sigma", "p0", "p1", "p2");
    sigBgFnc->SetNpx(1000);
    _currentFunctions->Add(sigBgFnc);

    TF1 * bgFnc = new TF1("Pol2", Pol2, min, max, 3);
    // set parameter names
    bgFnc->SetParNames("p0", "p1", "p2");
    bgFnc->SetNpx(1000);
    _currentFunctions->Add(bgFnc);
  }
  else if (name.compare("GausPol2") == 0) {
    TF1 * sigBgFnc = new TF1("GausPol2", GausPol2, min, max, 6);
    // set parameter names
    sigBgFnc->SetParNames("yield", "mass", "width", "p0", "p1", "p2");
    sigBgFnc->SetNpx(1000);
    _currentFunctions->Add(sigBgFnc);

    TF1 * bgFnc = new TF1("Pol2", Pol2, min, max, 3);
    // set parameter names
    bgFnc->SetParNames("p0", "p1", "p2");
    bgFnc->SetNpx(1000);
    _currentFunctions->Add(bgFnc);
  }
  else {
    return nullptr;
  }

  return _currentFunctions;
}