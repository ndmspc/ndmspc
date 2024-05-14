#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <TCanvas.h>
#include <TList.h>
#include <THashList.h>

int NdmspcConvertStrangeness(std::string inFile       = "data_orig.root",
                             std::string inListName   = "Cascade_analysis/chists_Xim_",
                             std::string inObjRecName = "h3_ptmasscent_def", std::string outFile = "data.root",
                             bool isMC = false, Double_t mass = 1.32171)
{
  TFile * fIn = TFile::Open(inFile.c_str());
  if (!fIn) {
    Printf("Problem opening file '%s' !!!", inFile.c_str());
    return 1;
  }

  THashList * list = (THashList *)fIn->Get(inListName.c_str());
  if (!list) {
    Printf("Problem opening list '%s' !!!", inListName.c_str());
    return 2;
  }
  // list->Print();

  TH3 * hRec = (TH3 *)list->FindObject(inObjRecName.c_str());
  if (!hRec) {
    Printf("Problem opening object '%s' !!!", inObjRecName.c_str());
    return 3;
  }

  Int_t    bins[3] = {hRec->GetXaxis()->GetNbins(), hRec->GetYaxis()->GetNbins(), hRec->GetZaxis()->GetNbins()};
  Double_t xmin[3] = {hRec->GetXaxis()->GetXmin(), hRec->GetYaxis()->GetXmin(), hRec->GetZaxis()->GetXmin()};
  Double_t xmax[3] = {hRec->GetXaxis()->GetXmax(), hRec->GetYaxis()->GetXmax(), hRec->GetZaxis()->GetXmax()};

  THnSparse * sRec = nullptr;
  THnSparse * sGen = nullptr;
  if (isMC) {
    sRec = new THnSparseD("sTrue", "True", 3, bins, xmin, xmax);
    sGen = new THnSparseD("sGen", "Gen", 3, bins, xmin, xmax);
  }
  else {
    sRec = new THnSparseD("sRec", "rec", 3, bins, xmin, xmax);
  }

  if (hRec->GetXaxis()->GetXbins()->GetArray())
    sRec->GetAxis(0)->Set(hRec->GetXaxis()->GetNbins(), hRec->GetXaxis()->GetXbins()->GetArray());
  if (hRec->GetYaxis()->GetXbins()->GetArray())
    sRec->GetAxis(1)->Set(hRec->GetYaxis()->GetNbins(), hRec->GetYaxis()->GetXbins()->GetArray());
  if (hRec->GetYaxis()->GetXbins()->GetArray())
    sRec->GetAxis(2)->Set(hRec->GetZaxis()->GetNbins(), hRec->GetZaxis()->GetXbins()->GetArray());

  if (isMC) {

    if (hRec->GetXaxis()->GetXbins()->GetArray())
      sGen->GetAxis(0)->Set(hRec->GetXaxis()->GetNbins(), hRec->GetXaxis()->GetXbins()->GetArray());
    if (hRec->GetYaxis()->GetXbins()->GetArray())
      sGen->GetAxis(1)->Set(hRec->GetYaxis()->GetNbins(), hRec->GetYaxis()->GetXbins()->GetArray());
    if (hRec->GetYaxis()->GetXbins()->GetArray())
      sGen->GetAxis(2)->Set(hRec->GetZaxis()->GetNbins(), hRec->GetZaxis()->GetXbins()->GetArray());
  }

  Printf("Object is 3D histogram %d %d %d", hRec->GetXaxis()->GetNbins(), hRec->GetYaxis()->GetNbins(),
         hRec->GetZaxis()->GetNbins());
  Int_t point[3];
  for (Int_t i = 0; i <= hRec->GetXaxis()->GetNbins() + 1; i++) {
    point[0] = i;
    for (Int_t j = 0; j <= hRec->GetYaxis()->GetNbins() + 1; j++) {
      point[1] = j;
      for (Int_t k = 0; k <= hRec->GetZaxis()->GetNbins() + 1; k++) {
        point[2] = k;

        if (hRec->GetBinContent(point[0], point[1], point[2]) > -10) {
          Long64_t currBin = sRec->GetBin(point, true);
          sRec->SetBinContent(currBin, hRec->GetBinContent(point[0], point[1], point[2]));
          sRec->SetBinError(currBin, hRec->GetBinError(point[0], point[1], point[2]));
          // if (!k)
          // Printf("%lld %d %d %d %f %f %f %f", currBin, point[0], point[1], point[2],
          //        hRec->GetBinContent(point[0], point[1], point[2]), sRec->GetBinContent(currBin),
          //        hRec->GetBinError(point[0], point[1], point[2]), sRec->GetBinError(currBin));
        }
        else {
          Printf("%d %d %d %f", point[0], point[1], point[2], hRec->GetBinContent(point[0], point[1], point[2]));
        }
      }
    }
  }

  sRec->SetEntries(hRec->GetEntries());
  sRec->GetAxis(0)->SetNameTitle("pt", "p_{T}");
  sRec->GetAxis(1)->SetNameTitle("im", "IM");
  sRec->GetAxis(2)->SetNameTitle("mult", "N");

  if (isMC) {

    TH2 * hGen = (TH2 *)list->FindObject("h2_gen");
    if (!hGen) {
      Printf("Problem opening object 'h2_gen' !!!");
      return 5;
    }
    hGen->Draw("colz");
    point[1] = sGen->GetAxis(1)->FindBin(mass);
    for (Int_t i = 0; i <= hRec->GetXaxis()->GetNbins() + 1; i++) {
      point[0] = i;
      for (Int_t j = 0; j <= hRec->GetYaxis()->GetNbins() + 1; j++) {
        point[2] = j;

        // Printf("%d %d %f", i, j, hGen->GetBinContent(i, j));
        Long64_t currBin = sGen->GetBin(point, true);
        sGen->SetBinContent(currBin, hGen->GetBinContent(point[0], point[2]));
        sGen->SetBinError(currBin, hGen->GetBinError(point[0], point[2]));
      }
    }

    sGen->SetEntries(hRec->GetEntries());
    sGen->GetAxis(0)->SetNameTitle("pt", "p_{T}");
    sGen->GetAxis(1)->SetNameTitle("im", "IM");
    sGen->GetAxis(2)->SetNameTitle("mult", "N");
  }

  // TCanvas * cx = new TCanvas("x", "x");
  // cx->Divide(2, 1);
  // cx->cd(1);
  // hRec->Project3D("x")->DrawCopy("hist text");
  // cx->cd(2);
  // sRec->Projection(0, "O")->DrawCopy("hist text");
  // TCanvas * cy = new TCanvas("y", "y");
  // cy->Divide(2, 1);
  // cy->cd(1);
  // hRec->Project3D("y")->DrawCopy("hist text");
  // cy->cd(2);
  // sRec->Projection(1, "O")->DrawCopy("hist text");
  // TCanvas * cz = new TCanvas("z", "z");
  // cz->Divide(2, 1);
  // cz->cd(1);
  // hRec->Project3D("z")->DrawCopy("hist text");
  // cz->cd(2);
  // sRec->Projection(2, "O")->DrawCopy("hist text");

  // TCanvas * cxyz = new TCanvas("xyz", "xyz");
  // cxyz->Divide(2, 1);
  // cxyz->cd(1);
  // hRec->DrawCopy("colz");
  // cxyz->cd(2);
  // sRec->Projection(0, 1, 2, "O")->DrawCopy("colz");

  // fIn->Close();
  TFile * fOut = TFile::Open(outFile.c_str(), "RECREATE");
  sRec->Write();
  if (sGen) sGen->Write();

  fOut->Close();

  Printf("File '%s' was saved ...", outFile.c_str());
  return 0;
}
