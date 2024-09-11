
void rootlogon() {
  gROOT->ProcessLine(".include .");
  gROOT->LoadMacro("NdmspcPointMacro.C");
}
