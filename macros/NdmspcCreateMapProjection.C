#include <TAxis.h>
#include <TFile.h>
#include <TH2D.h>
#include <TH3D.h>
#include <THnSparse.h>
#include <TObjArray.h>
#include <TObjString.h>
#include <TString.h>
#include <TSystem.h>
using namespace std;
Int_t NdmspcCreateMapProjection(
    TString projections  = "axis1-pt_axis2-m",
    TString hMapFileName = "root://eos.ndmspc.io//eos/ndmspc/scratch/ndmspc/demo/phi/hMap.root",
    TString hMapObjName = "hMap", TString hMapProjObjName = "hProjMap", Double_t fill = 1,
    TString fileOpt = "?remote=1")
{
  Int_t histMinimum = 0;
  Int_t histMaximum = 2;

  TFile * fIn = TFile::Open(hMapFileName.Data());
  if (!fIn) {
    Printf("Error: Could not open input file '%s' !!!", hMapFileName.Data());
    return 1;
  }

  THnSparse * hIn = (THnSparse *)fIn->Get(hMapObjName.Data());

  TUrl    url(hMapFileName.Data());
  TString outHost = url.GetHost();

  // Fix if running on eos storage directly
  if (!outHost.CompareTo(gSystem->HostName())) outHost = "localhost";

  TString output = TString::Format("root://%s//%s/%s/%s.root", outHost.Data(), gSystem->DirName(url.GetFile()),
                                   projections.Data(), hMapProjObjName.Data());

  TFile * fOut = TFile::Open(TString::Format("%s%s", output.Data(), fileOpt.Data()).Data(), "RECREATE");
  if (!fOut) {
    Printf("Error: Could not open output file '%s' !!!", output.Data());
    return 2;
  }

  TObjArray * names = projections.Tokenize("_");

  // Printf("%s", ((TObjString *)names->At(0))->GetString().Data());

  if (names->GetEntriesFast() == 1) {
    Int_t ix = hIn->GetListOfAxes()->IndexOf(
        hIn->GetListOfAxes()->FindObject(((TObjString *)names->At(0))->GetString().Data()));
    TH1D * h1D = hIn->Projection(ix);
    h1D->SetNameTitle(hMapProjObjName.Data(), "");
    h1D->SetStats(kFALSE);
    h1D->SetFillColor(kYellow);
    h1D->SetMinimum(histMinimum);
    h1D->SetMaximum(histMaximum);
    TAxis * a = hIn->GetAxis(ix);
    h1D->GetXaxis()->SetNameTitle(a->GetName(), a->GetName());
    if (fill > 0) {
      for (Int_t i = 0; i < h1D->GetXaxis()->GetNbins(); i++) {
        h1D->SetBinContent(i + 1, fill);
        h1D->GetXaxis()->SetBinLabel(i + 1, hIn->GetAxis(ix)->GetBinLabel(i + 1));
      }
    }
    h1D->Write();
  }
  else if (names->GetEntriesFast() == 2) {
    Int_t ix = hIn->GetListOfAxes()->IndexOf(
        hIn->GetListOfAxes()->FindObject(((TObjString *)names->At(0))->GetString().Data()));
    Int_t iy = hIn->GetListOfAxes()->IndexOf(
        hIn->GetListOfAxes()->FindObject(((TObjString *)names->At(1))->GetString().Data()));
    TH2D * h2D = hIn->Projection(iy, ix);
    h2D->SetNameTitle(hMapProjObjName.Data(), "");
    h2D->SetStats(kFALSE);
    h2D->SetMinimum(histMinimum);
    h2D->SetMaximum(histMaximum);
    TAxis * a = hIn->GetAxis(ix);
    h2D->GetXaxis()->SetNameTitle(a->GetName(), a->GetName());
    a = hIn->GetAxis(iy);
    h2D->GetYaxis()->SetNameTitle(a->GetName(), a->GetName());
    if (fill) {
      for (Int_t i = 0; i < h2D->GetXaxis()->GetNbins(); i++) {
        h2D->GetXaxis()->SetBinLabel(i + 1, hIn->GetAxis(ix)->GetBinLabel(i + 1));
        for (Int_t j = 0; j < h2D->GetYaxis()->GetNbins(); j++) {
          h2D->SetBinContent(i + 1, j + 1, fill);
          if (i == 0) {
            h2D->GetYaxis()->SetBinLabel(j + 1, hIn->GetAxis(iy)->GetBinLabel(j + 1));
          }
        }
      }
    }
    h2D->Write();
  }

  else if (names->GetEntriesFast() == 3) {
    Int_t ix = hIn->GetListOfAxes()->IndexOf(
        hIn->GetListOfAxes()->FindObject(((TObjString *)names->At(0))->GetString().Data()));
    Int_t iy = hIn->GetListOfAxes()->IndexOf(
        hIn->GetListOfAxes()->FindObject(((TObjString *)names->At(1))->GetString().Data()));
    Int_t iz = hIn->GetListOfAxes()->IndexOf(
        hIn->GetListOfAxes()->FindObject(((TObjString *)names->At(2))->GetString().Data()));
    TH3D * h3D = hIn->Projection(ix, iy, iz);
    h3D->SetNameTitle(hMapProjObjName.Data(), "");
    h3D->SetStats(kFALSE);
    h3D->SetMinimum(histMinimum);
    h3D->SetMaximum(histMaximum);
    TAxis * a = hIn->GetAxis(ix);
    h3D->GetXaxis()->SetNameTitle(a->GetName(), a->GetName());
    a = hIn->GetAxis(iy);
    h3D->GetYaxis()->SetNameTitle(a->GetName(), a->GetName());
    a = hIn->GetAxis(iz);
    h3D->GetZaxis()->SetNameTitle(a->GetName(), a->GetName());

    if (fill) {
      for (Int_t i = 0; i < h3D->GetXaxis()->GetNbins(); i++) {
        h3D->GetXaxis()->SetBinLabel(i + 1, hIn->GetAxis(ix)->GetBinLabel(i + 1));
        for (Int_t j = 0; j < h3D->GetYaxis()->GetNbins(); j++) {
          if (i == 0) {
            h3D->GetYaxis()->SetBinLabel(j + 1, hIn->GetAxis(iy)->GetBinLabel(j + 1));
          }

          for (Int_t k = 0; k < h3D->GetZaxis()->GetNbins(); k++) {
            h3D->SetBinContent(i + 1, j + 1, k + 1, fill);
            if (i == 0 && j == 0) {
              h3D->GetZaxis()->SetBinLabel(k + 1, hIn->GetAxis(iz)->GetBinLabel(k + 1));
            }
          }
        }
      }
    }

    h3D->Write();
  }

  fIn->Close();
  fOut->Close();

  Printf("Output file: %s", output.Data());

  return 0;
}
