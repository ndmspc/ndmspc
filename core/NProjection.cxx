#include <vector>
#include <set>
#include "TString.h"
#include <TSystem.h>
#include <THStack.h>
#include <TCanvas.h>
#include <TROOT.h>
// #include <TGClient.h>
// #include <TGFrame.h>
#include <THnSparse.h>
#include <TVirtualPad.h>
#include "NProjection.h"
#include "NLogger.h"
#include "NUtils.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NProjection);
/// \endcond

namespace Ndmspc {
// NProjection::NProjection() : NHnSparseObject() {}
// NProjection::~NProjection() {}
void NProjection::Draw(Option_t * option) const
{
  ///
  /// Draws the NProjection object (default)
  ///

  std::vector<int> projIds;
  for (int i = 0; i < fBinning->GetAxes().size(); i++) {
    projIds.push_back(i);
  }
  Draw(projIds, option);
}
void NProjection::Draw(std::vector<std::string> projNames, Option_t * option) const
{
  ///
  /// Draws the NProjection object with the specified projection
  ///

  std::vector<int> projIds = fBinning->GetAxisIndexesByNames(projNames);
  Draw(projIds, option);
}

void NProjection::Draw(std::vector<int> projIds, Option_t * option) const
{
  ///
  /// Draws the NProjection object with the specified projection IDs
  ///

  Ndmspc::NLogger::Debug("Projection IDs: %s", NUtils::GetCoordsString(projIds, -1).c_str());
  // return;

  int       canvsCounter = 0;
  TCanvas * c            = nullptr;

  THnSparse * hnsObjContent = fBinning->GetContent();
  hnsObjContent->Print("all");

  std::vector<int> dims;
  for (size_t i = 0; i < projIds.size(); i++) {
    dims.push_back(projIds[i] * 3 + 2); // +2 because first two dimensions are reserved for mass and pt
  }
  Ndmspc::NLogger::Debug("Projection IDs: %s", NUtils::GetCoordsString(dims, -1).c_str());

  // std::vector<int> dims = {2, 5};
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
    Double_t    v         = hnsObjContent->GetBinContent(linBin, p);
    Long64_t    idx       = hnsObjContent->GetBin(p);
    std::string binCoords = NUtils::GetCoordsString(NUtils::ArrayToVector(p, hnsObjContent->GetNdimensions()), -1);
    Ndmspc::NLogger::Info("Bin %lld(%lld): %f %s", linBin, idx, v, binCoords.c_str());
    dimsResults[0].insert(p[dims[0]]);
    if (dims.size() > 1) dimsResults[1].insert(p[dims[1]]);
    if (dims.size() > 2) dimsResults[2].insert(p[dims[2]]);
  }

  if (!gROOT->IsBatch()) {
    // Ensure gClient is initialized if not already
    if (!gClient) {
      // This will initialize gClient if needed
      (void)gClient;
    }
  }

  Int_t screenWidth  = gClient->GetDisplayWidth();
  Int_t screenHeight = gClient->GetDisplayHeight();

  // Create a canvas that is, for example, 40% of the screen width and height
  constexpr double canvasScale  = 0.4;
  Int_t            canvasWidth  = static_cast<Int_t>(screenWidth * canvasScale);
  Int_t            canvasHeight = static_cast<Int_t>(screenHeight * canvasScale);

  Ndmspc::NLogger::Info("Screen size: %dx%d", screenWidth, screenHeight);
  // if (dims.size() <= 2) {
  //   dims.push_back(0); // Add a dummy dimension for 2D plotting
  // }
  int nCanvases = dims.size() > 2 ? dimsResults[2].size() : 1;
  Ndmspc::NLogger::Info("Number of canvases: %d", nCanvases);
  std::vector<std::string> projNames = fBinning->GetAxisNamesByIndexes(projIds);
  std::string              posfix    = NUtils::Join(projNames, '_');
  for (int iCanvas = 0; iCanvas < nCanvases; iCanvas++) {
    if (c == nullptr) {
      canvsCounter++;
      // Ndmspc::NLogger::Info("Creating canvas for dimension %d: %s", dims[2],
      //                       hnsObjContent->GetAxis(dims[2])->GetName());

      std::string canvasName = Form("c_%s", posfix.c_str());
      NLogger::Info("Creating canvas '%s' with size %dx%d", canvasName.c_str(), canvasWidth, canvasHeight);
      c           = new TCanvas(canvasName.c_str(), canvasName.c_str(), canvasWidth, canvasHeight);
      int divideX = int(TMath::Sqrt(nCanvases));

      int divideY = (nCanvases / divideX);
      if ((nCanvases % divideX) != 0) divideY++;
      c->Divide(divideX, divideY);
    }

    std::string stackName  = Form("hStack_%s_%d", posfix.c_str(), iCanvas);
    std::string stackTitle = "";
    stackTitle += projNames[0];
    stackTitle += projNames.size() > 1 ? " vs " + projNames[1] : "";
    if (dims.size() > 2) {
      p[dims[2]] = iCanvas + 1; // 1-based index for the third dimension
      double min, max;
      fBinning->GetAxisRange(projIds[2], min, max, {p[projIds[2] * 3], p[projIds[2] * 3 + 1], p[projIds[2] * 3 + 2]});
      stackTitle += projNames.size() > 2 ? " for " + projNames[2] + " " + Form(" [%f,%f]", min, max) : "";
    }
    NLogger::Info("Creating stack '%s' with title '%s'", stackName.c_str(), stackTitle.c_str());

    THStack * hStack  = new THStack(stackName.c_str(), stackTitle.c_str());
    int       nStacks = dims.size() > 1 ? dimsResults[1].size() : 1;
    for (int iStack = 0; iStack < nStacks; iStack++) {
      // c->cd(iStack + 1);
      p[dims[0]] = 0;
      if (dims.size() > 1) p[dims[1]] = iStack + 1; // 1-based index for the second dimension
      // if (dims.size() > 2) p[dims[2]] = iCanvas + 1; // 1-based index for the third dimension
      linBin     = hnsObjContent->GetBin(p);
      TH1 * hTmp = (TH1 *)GetObject("mass", linBin);
      if (hTmp == nullptr) {
        Ndmspc::NLogger::Error("No histogram found for bin %lld", linBin);
        continue;
      }

      if (projIds.size() > 1) {
        double min, max;
        fBinning->GetAxisRange(projIds[1], min, max, {p[projIds[1] * 3], p[projIds[1] * 3 + 1], p[projIds[1] * 3 + 2]});
        std::string histTitle = projNames.size() > 0 ? projNames[1] + " " + Form(" [%.3f,%.3f]", min, max) : "";
        hTmp->SetTitle(Form("%s", histTitle.c_str()));
      }
      else {
        hTmp->SetTitle("");
      }
      hTmp->Print();
      hTmp->SetMarkerStyle(20);
      hTmp->SetMarkerColor(iStack + 1);
      // hTmp->GetXaxis()->SetRangeUser(0.0, 8.0);

      hStack->Add((TH1 *)hTmp->Clone());
    }

    c->cd(iCanvas + 1);
    hStack->SetMinimum(1.015);
    hStack->SetMaximum(1.023);
    hStack->Draw("nostack");
    // hStack->GetXaxis()->SetRangeUser(0.0, 8.0);
    if (dims.size() > 1) gPad->BuildLegend(0.75, 0.75, 0.95, 0.95, "");
    c->ModifiedUpdate();
    gSystem->ProcessEvents();
  }

  for (size_t i = 0; i < dims.size(); i++) {
    Ndmspc::NLogger::Info("Dimension %d: %zu unique values", dims[i], dimsResults[i].size());
    for (auto & v : dimsResults[i]) {
      Ndmspc::NLogger::Info("  Value: %d", v);
    }
  }
}

} // namespace Ndmspc
