#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <THnSparse.h>
#include <TCanvas.h>
#include <TGrid.h>
#include <TROOT.h>

#include <sstream>
#include <vector>

THnSparse * sparse        = nullptr;
THnSparse * sparseTmp     = nullptr;
TObjArray * listOfSparses = new TObjArray();

void HighlightAxes(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);

int NdmspcAxisBrowser(
    std::string file   = "AnalysisResults.root",
    std::string object = "phianalysis-t-hn-sparse_default/unlikepm,phianalysis-t-hn-sparse_default/likepp",
    std::string token  = "/")
{
  TH1::AddDirectory(false);

  std::vector<std::string> objects;
  std::vector<std::string> labelWOworkflow;
  std::stringstream        ss(object);
  std::string              label;
  char                     delimiter = ',';

  if (file.rfind("alien://", 0) == 0) {
    TGrid::Connect("alien://");
  }
  while (std::getline(ss, label, delimiter)) {
    objects.push_back(label);
  }

  // for (const auto & o : objects) {
  //   Printf("%s", o.c_str());
  // }

  for (int i = 0; i < objects.size(); i++) {
    std::string objTemp = objects[i].c_str();
    size_t      pos     = objTemp.find(token);
    labelWOworkflow.push_back(objTemp.substr(pos + token.length()));
    Printf("%s", labelWOworkflow[i].c_str());
  }

  TFile * inputFile = TFile::Open(file.c_str());
  if (!inputFile) {
    Printf("Error: Cannot open file %s!", file.c_str());
    return 2;
  }
  sparse = (THnSparse *)inputFile->Get(objects[0].c_str());
  if (!sparse) {
    Printf("Error: Cannot find object %s!", objects[0].c_str());
    return 3;
  }

  TObjArray * listOfAxes = sparse->GetListOfAxes();
  int         nDim       = listOfAxes->GetEntries();

  TH2 * hAxes = new TH2D("hAxes", "Sparse Axes", nDim, 0., nDim, objects.size(), 0., objects.size());
  hAxes->SetMaximum(2);

  for (int i = 0; i < nDim; i++) {
    TAxis * a = (TAxis *)listOfAxes->At(i);
    hAxes->GetXaxis()->SetBinLabel(i + 1, a->GetName());
    for (int j = 0; j < objects.size(); j++) {
      hAxes->SetBinContent(i + 1, j + 1, 1);
    }
  }

  for (int i = 0; i < objects.size(); i++) {
    if (token.empty())
      hAxes->GetYaxis()->SetBinLabel(i + 1, objects[i].c_str());
    else
      hAxes->GetYaxis()->SetBinLabel(i + 1, labelWOworkflow[i].c_str());
  }

  hAxes->GetYaxis()->SetLabelSize(0.08);
  hAxes->GetXaxis()->SetLabelSize(0.08);
  hAxes->SetStats(0);

  for (int i = 0; i < objects.size(); i++) {
    sparseTmp = (THnSparse *)inputFile->Get(objects[i].c_str());
    if (!sparseTmp) {
      Printf("Error: Cannot find object %s!", objects[i].c_str());
      continue;
    }
    listOfSparses->Add(sparseTmp);
  }

  TCanvas * CanvasMap = new TCanvas("CanvasAxes", "CanvasAxes", 0, 0, 1000, 300);
  CanvasMap->HighlightConnect("HighlightAxes(TVirtualPad*,TObject*,Int_t,Int_t)");
  hAxes->SetHighlight(true);
  hAxes->Draw("colz");

  inputFile->Close();
  delete inputFile;

  return 0;
}
void HighlightAxes(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  // Printf("Obj = %p %d %d", (void *)obj, xBin, yBin);
  auto hAxes = dynamic_cast<TH1D *>(obj);

  auto CanvasProj = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProj");
  if (hAxes && !hAxes->IsHighlight()) { // after highlight disabled
    if (CanvasProj) delete CanvasProj;
    hAxes->SetTitle("Disable highlight");
    return;
  }

  if (!CanvasProj) {
    CanvasProj = new TCanvas("CanvasProj", "CanvasProj", 1005, 0, 600, 600);
  }
  sparse = dynamic_cast<THnSparse *>(listOfSparses->At(yBin - 1));
  if (!sparse) {
    Printf("Error: Cannot find THnSparse at index %d", yBin - 1);
    return;
  }
  TH1 * proj = sparse->Projection(xBin - 1);
  if (!proj) {
    Printf("Error: Cannot project THnSparse at bin %d", xBin - 1);
    return;
  }

  CanvasProj->cd();
  proj->Draw();
  CanvasProj->Modified();
  CanvasProj->Update();
}
