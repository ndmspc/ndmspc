#include <string>
#include <vector>
#include <THStack.h>
#include <TCanvas.h>
#include <TROOT.h>
#include <TGClient.h>
#include <TGFrame.h> // For TGMainFrame
#include <THnSparse.h>
#include <TVirtualPad.h>
#include "NHnSparseTree.h"
#include "MyProcessResults.C"

void NHnSparseTreeProcessResults(int nThreads = 1, std::string filename = "$HOME/.ndmspc/dev/hnst_out.root",
                                 std::string enabledBranches = "")
{

  Ndmspc::NHnSparseTree * hnstIn = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnstIn == nullptr) {
    return;
  }
  // hnstIn->Print("P");
  // return;

  Ndmspc::NHnSparseTree * hnstOut = new Ndmspc::NHnSparseTreeC("$HOME/.ndmspc/dev/hnst_out_results.root");
  if (hnstOut == nullptr) {
    Ndmspc::NLogger::Error("Cannot create output HnSparseTree");
    return;
  }

  std::vector<TAxis *> axesIn  = hnstIn->GetBinning()->GetAxes();
  TObjArray *          axesOut = new TObjArray();
  for (auto & axis : axesIn) {
    TAxis * axisOut = (TAxis *)axis->Clone();
    axesOut->Add(axisOut);
  }
  hnstOut->InitAxes(axesOut);

  // Get binning definition from input HnSparseTree
  std::map<std::string, std::vector<std::vector<int>>> b = hnstIn->GetBinning()->GetDefinition();
  // print size of b
  Ndmspc::NLogger::Info("Binning size: %zu", b.size());

  hnstOut->ImportBinning(b);
  hnstOut->Print();
  // return;

  if (hnstOut->GetBinning() == nullptr) {
    Ndmspc::NLogger::Error("Binning is not initialized in output HnSparseTree");
    return;
  }

  if (hnstOut->GetBinning()->GetContent()->GetNbins() <= 0) {
    Ndmspc::NLogger::Error("No bins in output HnSparseTree");
    return;
  }
  std::vector<int> mins = {1};
  std::vector<int> maxs = {(int)hnstIn->GetBinning()->GetContent()->GetNbins()};
  // mins[0]               = 0;
  // maxs[0] = 400;
  // maxs[0] = 1;
  //
  // hnst->Close();

  hnstOut->Process(NdmspcUserProcessResults, mins, maxs, nThreads, hnstIn, hnstOut);

  TCanvas * c = nullptr;

  Int_t                     padIndex      = 1;
  Int_t                     canvsCounter  = 0;
  Ndmspc::NHnSparseObject * hnsObj        = hnstIn->GetPoint()->GetHnSparseObject();
  THnSparse *               hnsObjContent = hnsObj->GetBinning()->GetContent();
  hnsObjContent->Print("all");

  std::vector<int> dims = {2, 5};
  // std::vector<int> dims = {2, 5, 8};
  // std::vector<int> dims = {2, 8, 5};
  // std::vector<int> dims = {5, 2, 8};
  // std::vector<int> dims = {5, 8, 2};
  // std::vector<int>           dims = {8, 2, 5};
  std::vector<std::set<int>> dimsResults(3);

  hnsObjContent->GetAxis(dims[0])->SetRange(-1, 0);
  if (dims.size() > 1) hnsObjContent->GetAxis(dims[1])->SetRange(1, hnsObjContent->GetAxis(dims[1])->GetNbins());
  if (dims.size() > 2) hnsObjContent->GetAxis(dims[2])->SetRange(1, hnsObjContent->GetAxis(dims[2])->GetNbins());
  Int_t *                                         p      = new Int_t[hnsObjContent->GetNdimensions()];
  Long64_t                                        linBin = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{hnsObjContent->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t    v   = hnsObjContent->GetBinContent(linBin, p);
    Long64_t    idx = hnsObjContent->GetBin(p);
    std::string binCoords =
        Ndmspc::NUtils::GetCoordsString(Ndmspc::NUtils::ArrayToVector(p, hnsObjContent->GetNdimensions()), -1);
    Ndmspc::NLogger::Info("Bin %lld(%lld): %f %s", linBin, idx, v, binCoords.c_str());
    dimsResults[0].insert(p[dims[0]]);
    if (dims.size() > 1) dimsResults[1].insert(p[dims[1]]);
    if (dims.size() > 2) dimsResults[2].insert(p[dims[2]]);
  }

  if (!gROOT->IsBatch()) {
    gClient; // Accessing gClient initializes it if not already
  }

  Int_t screenWidth  = gClient->GetDisplayWidth();
  Int_t screenHeight = gClient->GetDisplayHeight();

  // Create a canvas that is, for example, 90% of the screen width and height
  double canvasScale  = 0.4; // Scale factor for canvas size
  Int_t  canvasWidth  = static_cast<Int_t>(screenWidth * canvasScale);
  Int_t  canvasHeight = static_cast<Int_t>(screenHeight * canvasScale);

  Ndmspc::NLogger::Info("Screen size: %dx%d", screenWidth, screenHeight);
  // if (dims.size() <= 2) {
  //   dims.push_back(0); // Add a dummy dimension for 2D plotting
  // }
  int nCanvases = dims.size() > 2 ? dimsResults[2].size() : 1;
  Ndmspc::NLogger::Info("Number of canvases: %d", nCanvases);
  for (int iCanvas = 0; iCanvas < nCanvases; iCanvas++) {
    if (c == nullptr) {
      canvsCounter++;
      // Ndmspc::NLogger::Info("Creating canvas for dimension %d: %s", dims[2],
      //                       hnsObjContent->GetAxis(dims[2])->GetName());

      std::string canvasName = Form("c_%d", canvsCounter);
      c                      = new TCanvas(canvasName.c_str(), canvasName.c_str(), canvasWidth, canvasHeight);
      int divideX            = int(TMath::Sqrt(nCanvases));

      int divideY = (nCanvases / divideX);
      if ((nCanvases % divideX) != 0) divideY++;
      c->Divide(divideX, divideY);
    }

    THStack * hStack = new THStack(Form("hStack_%d", iCanvas), Form("Stack_%d", iCanvas));
    for (int iStack = 0; iStack < dimsResults[1].size(); iStack++) {
      // c->cd(iStack + 1);
      p[dims[0]] = 0;
      p[dims[1]] = iStack + 1;                       // 1-based index for the second dimension
      if (dims.size() > 2) p[dims[2]] = iCanvas + 1; // 1-based index for the third dimension
      linBin     = hnsObjContent->GetBin(p);
      TH1 * hTmp = (TH1 *)hnsObj->GetObject("mass", linBin);
      hTmp->Print("");
      hTmp->SetMarkerStyle(20);
      hTmp->SetMarkerColor(iStack + 1);
      // hTmp->GetXaxis()->SetRangeUser(0.0, 8.0);

      hStack->Add(hTmp);
    }

    c->cd(iCanvas + 1);
    hStack->SetMinimum(1.015);
    hStack->SetMaximum(1.023);
    hStack->Draw("nostack");
    // hStack->GetXaxis()->SetRangeUser(0.0, 8.0);
    gPad->BuildLegend(0.75, 0.75, 0.95, 0.95, "");
    gPad->ModifiedUpdate();
    gSystem->ProcessEvents();
  }

  for (size_t i = 0; i < dims.size(); i++) {
    Ndmspc::NLogger::Info("Dimension %d: %zu unique values", dims[i], dimsResults[i].size());
    for (auto & v : dimsResults[i]) {
      Ndmspc::NLogger::Info("  Value: %d", v);
    }
  }
  // hnsObjContent->Projection(0)->Draw();
  return;

  for (Long64_t i = 0; i < hnsObjContent->GetNbins(); i++) {
    TObject * obj = hnsObj->GetObject("mass", i);
    if (!obj) {
      Ndmspc::NLogger::Error("Cannot get object 'mass' from HnSparseObject");
      return;
    }

    if (c == nullptr) {
      canvsCounter++;
      std::string canvasName = Form("c_%d", canvsCounter);
      c                      = new TCanvas(canvasName.c_str(), canvasName.c_str(), 1500, 1000);
    }

    Ndmspc::NLogger::Info("Drawing object '%s' with index %lld", obj->GetName(), i);
    TH1 * h = dynamic_cast<TH1 *>(obj);
    h->GetYaxis()->SetRangeUser(1.018, 1.022);
    c->cd(padIndex);
    h->DrawCopy("");
    gPad->ModifiedUpdate();
    // gSystem->Sleep(1000);
    gSystem->ProcessEvents();

    if (padIndex < 16) {
      padIndex++;
    }
    else {
      c->Update();
      c->SaveAs(Form("/tmp/hnst_out_results_%d.pdf", canvsCounter));
      // SafeDelete(c);
      c        = nullptr;
      padIndex = 1;
    }
  }
  hnstOut->Close(true);
}
