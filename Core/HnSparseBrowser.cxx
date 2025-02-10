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
ClassImp(Ndmspc::HnSparseBrowser);
/// \endcond

namespace Ndmspc {
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

int HnSparseBrowser::DrawBrowser(std::string filename, std::string objects, std::string dirtoken)
{
  ///
  /// Draw
  ///

  TApplication app("app", nullptr, nullptr);
  TH1::AddDirectory(false);

  std::vector<std::string> objectList;
  std::vector<std::string> labels;
  std::stringstream        ss(objects);
  std::string              label;
  char                     delimiter = ',';

  if (filename.rfind("alien://", 0) == 0) {
    TGrid::Connect("alien://");
  }
  while (std::getline(ss, label, delimiter)) {
    objectList.push_back(label);
  }

  // for (const auto & o : objects) {
  //   Printf("%s", o.c_str());
  // }

  Printf("Opening file '%s' ...", filename.c_str());
  TFile * inputFile = Ndmspc::Utils::OpenFile(filename.c_str());
  if (!inputFile) {
    Printf("Error: Cannot open filename %s!", filename.c_str());
    return 2;
  }

  for (long unsigned int i = 0; i < objectList.size(); i++) {
    std::string objTemp = objectList[i].c_str();
    Printf("Object : %s", objTemp.c_str());
    size_t pos = objTemp.find(dirtoken);
    labels.push_back(objTemp.substr(pos + dirtoken.length()));
  }

  fInputHnSparse = (THnSparse *)inputFile->Get(objectList[0].c_str());
  if (!fInputHnSparse) {
    Printf("Error: Cannot find objects %s!", objectList[0].c_str());
    return 3;
  }

  TObjArray * listOfAxes = fInputHnSparse->GetListOfAxes();
  int         nDim       = listOfAxes->GetEntries();

  TH2 * hAxes = new TH2D("hAxes", "Sparse Axes", nDim, 0., nDim, objectList.size(), 0., objectList.size());
  hAxes->SetMinimum(0);
  hAxes->SetMaximum(1);

  for (int i = 0; i < nDim; i++) {
    TAxis * a = (TAxis *)listOfAxes->At(i);
    hAxes->GetXaxis()->SetBinLabel(i + 1, a->GetName());
    for (long unsigned int j = 0; j < objects.size(); j++) {
      hAxes->SetBinContent(i + 1, j + 1, 1);
    }
  }

  for (long unsigned int i = 0; i < objects.size(); i++) {
    if (dirtoken.empty())
      hAxes->GetYaxis()->SetBinLabel(i + 1, objectList[i].c_str());
    else
      hAxes->GetYaxis()->SetBinLabel(i + 1, labels[i].c_str());
  }

  hAxes->GetYaxis()->SetLabelSize(0.08);
  hAxes->GetXaxis()->SetLabelSize(0.08);
  hAxes->SetStats(0);
  THnSparse * sparseTmp = nullptr;
  for (long unsigned int i = 0; i < objects.size(); i++) {
    sparseTmp = (THnSparse *)inputFile->Get(objectList[i].c_str());
    if (!sparseTmp) {
      Printf("Error: Cannot find objects %s!", objectList[i].c_str());
      continue;
    }
    fListOfHnSparses->Add(sparseTmp);
  }

  TCanvas * CanvasMap = new TCanvas("CanvasAxes", "CanvasAxes", 0, 0, 1000, 300);
  // CanvasMap->HighlightConnect("HighlightMain(TVirtualPad*,TObject*,Int_t,Int_t)");
  CanvasMap->Connect("Highlighted(TVirtualPad*,TObject*,Int_t,Int_t)", "Ndmspc::HnSparseBrowser", this,
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
  ///
  /// Main highlight function
  ///
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

} // namespace Ndmspc
