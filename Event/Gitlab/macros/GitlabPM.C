
#include <TROOT.h>
#include <TList.h>
#include <THnSparse.h>
#include <TH1D.h>
#include <ndmspc/PointRun.h>
#include <ndmspc/Utils.h>
bool GitlabPM(Ndmspc::PointRun *pr)
{
  json                     cfg          = pr->Cfg();
  TList *                  inputList    = pr->GetInputList();
  THnSparse *              resultObject = pr->GetResultObject();
  Int_t *                  point        = pr->GetCurrentPoint();
  std::vector<std::string> pointLabels  = pr->GetCurrentPointLabels();
  json                     pointValue   = pr->GetCurrentPointValue();
  TList *                  outputList   = pr->GetOutputList();
  
  int verbose = 0;
  if (!cfg["user"]["verbose"].is_null() && cfg["user"]["verbose"].is_number_integer()) {
    verbose = cfg["user"]["verbose"].get<int>();
  }
  
  THnSparse * hs = (THnSparse *)inputList->At(0);

  int projId = cfg["user"]["proj"].get<int>();
  TH1D *      h  = hs->Projection(projId, "O");
  
  TString titlePostfix = "(no cuts)";
  if (cfg["ndmspc"]["projection"]["title"].is_string())
    titlePostfix = cfg["ndmspc"]["projection"]["title"].get<std::string>();  
  h->SetNameTitle("h", TString::Format("h - %s", titlePostfix.Data()).Data());
  outputList->Add(h);

  // Skip bin (do not save any output)
  if (h->GetEntries() < cfg["user"]["minEntries"].get<int>()) 
    return false; 

  Double_t integral = h->Integral();
  if (verbose >= 0)
    Printf("Integral = %f ", integral);


  if (resultObject) {
     Ndmspc::Utils::SetResultValueError(cfg, resultObject, "Integral", point, integral, TMath::Sqrt(integral), false, true);
  }

  if (!gROOT->IsBatch() && !cfg["ndmspc"]["process"]["type"].get<std::string>().compare("single")) {
    h->DrawCopy();
  }

  return true;
}

