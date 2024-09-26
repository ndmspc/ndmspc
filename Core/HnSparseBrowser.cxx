#include <sstream>
#include <TString.h>
#include <TApplication.h>
#include <TCanvas.h>
#include <TRootCanvas.h>
#include <TROOT.h>
#include <TH2.h>
#include <TH1D.h>
#include <TCanvas.h>
#include <TGrid.h>
#include <THnSparse.h>
#include "Utils.h"
#include "HnSparseBrowser.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::HnSparseBrowser);
/// \endcond

namespace NdmSpc {
HnSparseBrowser::HnSparseBrowser() : TObject()

{
  ///
  /// Default constructor
  ///
  fListOfHnSparses = new TList();
}

HnSparseBrowser::~HnSparseBrowser()
{
  ///
  /// Descructor
  ///
  SafeDelete(fListOfHnSparses);
}

int HnSparseBrowser::Draw(std::string fInputFileName, std::string fInputObjects, std::string fDirectoryToken)
{
  ///
  /// Draw
  ///

  TApplication app("app", nullptr, nullptr);
  TH1::AddDirectory(false);

  std::vector<std::string> objects;
  std::vector<std::string> labels;
  std::stringstream        ss(fInputObjects);
  std::string              label;
  char                     delimiter = ',';

  if (fInputFileName.rfind("alien://", 0) == 0) {
    TGrid::Connect("alien://");
  }
  while (std::getline(ss, label, delimiter)) {
    objects.push_back(label);
  }

  // for (const auto & o : objects) {
  //   Printf("%s", o.c_str());
  // }

  Printf("Opening file '%s' ...", fInputFileName.c_str());
  TFile * inputFile = NdmSpc::Utils::OpenFile(fInputFileName.c_str());
  if (!inputFile) {
    Printf("Error: Cannot open fInputFileName %s!", fInputFileName.c_str());
    return 2;
  }

  for (int i = 0; i < objects.size(); i++) {
    std::string objTemp = objects[i].c_str();
    Printf("Object : %s", objTemp.c_str());
    size_t pos = objTemp.find(fDirectoryToken);
    labels.push_back(objTemp.substr(pos + fDirectoryToken.length()));
  }

  fInputHnSparse = (THnSparse *)inputFile->Get(objects[0].c_str());
  if (!fInputHnSparse) {
    Printf("Error: Cannot find fInputObjects %s!", objects[0].c_str());
    return 3;
  }

  TObjArray * listOfAxes = fInputHnSparse->GetListOfAxes();
  int         nDim       = listOfAxes->GetEntries();

  TH2 * hAxes = new TH2D("hAxes", "Sparse Axes", nDim, 0., nDim, objects.size(), 0., objects.size());
  hAxes->SetMinimum(0);
  hAxes->SetMaximum(1);

  for (int i = 0; i < nDim; i++) {
    TAxis * a = (TAxis *)listOfAxes->At(i);
    hAxes->GetXaxis()->SetBinLabel(i + 1, a->GetName());
    for (int j = 0; j < objects.size(); j++) {
      hAxes->SetBinContent(i + 1, j + 1, 1);
    }
  }

  for (int i = 0; i < objects.size(); i++) {
    if (fDirectoryToken.empty())
      hAxes->GetYaxis()->SetBinLabel(i + 1, objects[i].c_str());
    else
      hAxes->GetYaxis()->SetBinLabel(i + 1, labels[i].c_str());
  }

  hAxes->GetYaxis()->SetLabelSize(0.08);
  hAxes->GetXaxis()->SetLabelSize(0.08);
  hAxes->SetStats(0);
  THnSparse * sparseTmp = nullptr;
  for (int i = 0; i < objects.size(); i++) {
    sparseTmp = (THnSparse *)inputFile->Get(objects[i].c_str());
    if (!sparseTmp) {
      Printf("Error: Cannot find fInputObjects %s!", objects[i].c_str());
      continue;
    }
    fListOfHnSparses->Add(sparseTmp);
  }

  TCanvas * CanvasMap = new TCanvas("CanvasAxes", "CanvasAxes", 0, 0, 1000, 300);
  // CanvasMap->HighlightConnect("HighlightMain(TVirtualPad*,TObject*,Int_t,Int_t)");
  CanvasMap->Connect("Highlighted(TVirtualPad*,TObject*,Int_t,Int_t)", "NdmSpc::HnSparseBrowser", this,
                     "HighlightMain(TVirtualPad*,TObject*,Int_t,Int_t)");
  hAxes->SetHighlight(true);
  hAxes->Draw("colz");

  inputFile->Close();
  delete inputFile;

  app.Run();
  return 0;
}

void HnSparseBrowser::HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
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
  fInputHnSparse = dynamic_cast<THnSparse *>(fListOfHnSparses->At(yBin - 1));
  if (!fInputHnSparse) {
    Printf("Error: Cannot find THnSparse at index %d", yBin - 1);
    return;
  }
  TH1 * proj = fInputHnSparse->Projection(xBin - 1);
  if (!proj) {
    Printf("Error: Cannot project THnSparse at bin %d", xBin - 1);
    return;
  }

  CanvasProj->cd();
  proj->Draw();
  CanvasProj->Modified();
  CanvasProj->Update();
}

} // namespace NdmSpc
