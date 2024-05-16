#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TString.h>
#include <TList.h>
#include <TMacro.h>
#include <TROOT.h>
#include <TSystem.h>
#include <TCanvas.h>
#include <THnSparse.h>

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
json             cfg;
TFile *          fIn = nullptr;
THnSparse *      rh  = nullptr;
std::string      currentParameterName;
std::vector<int> parameterPoint;
TH1 *            hMap = nullptr;
std::string      mapTitle;
TH1 *            hParamMap = nullptr;
int              nDimCuts  = 0;
void             HighlightProj(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
void             HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
void             HighlightPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
int              NdmspcDrawResult(std::string configFile = "myAnalysis.json", std::string parameter = "",
                                  std::string resultsHnSparseName = "results", std::string parameterAxisName = "parameters",
                                  std::string inputFile = "")
{

  currentParameterName = parameter;

  TH1::AddDirectory(kFALSE);

  std::ifstream f(configFile);
  if (f.is_open()) {
    cfg = json::parse(f);
  }
  else {
    Printf("Error: Config file '%s' was not found !!!", configFile.c_str());
    return 1;
  }

  std::string hostUrl = cfg["ndmspc"]["output"]["host"].get<std::string>();
  if (hostUrl.empty()) {
    Printf("Error:  cfg[ndmspc][output][host] is empty!!!");
    return 2;
  }

  std::string path = hostUrl + "/" + cfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";

  for (auto & cut : cfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    path += cut["axis"].get<std::string>() + "_";
    nDimCuts++;
  }

  path[path.size() - 1] = '/';

  if (inputFile.empty()) inputFile = path + "results.root";

  Printf("Opening file '%s' ...", inputFile.c_str());

  fIn = TFile::Open(inputFile.c_str());
  if (!fIn) {
    Printf("Error: Input file '%s' was not found !!!", inputFile.c_str());
    return 2;
  }

  rh = (THnSparse *)fIn->Get(resultsHnSparseName.c_str());

  if (!rh) {
    Printf("Error: Results THnSparse histogram '%s' was not found !!!", resultsHnSparseName.c_str());
    return 3;
  }

  for (int iDim = 0; iDim < rh->GetNdimensions(); iDim++) parameterPoint.push_back(-1);

  if (currentParameterName.empty()) {
    int idxDefault       = cfg["ndmspc"]["result"]["parameters"]["default"].get<int>();
    currentParameterName = cfg["ndmspc"]["result"]["parameters"]["labels"][idxDefault].get<std::string>();
  }
  Printf("Paremeter: %s", currentParameterName.c_str());

  TAxis * a = (TAxis *)rh->GetListOfAxes()->FindObject(parameterAxisName.c_str());
  if (a == nullptr) {
    return 4;
  }
  Int_t id    = rh->GetListOfAxes()->IndexOf(a);
  Int_t idBin = a->FindBin(currentParameterName.c_str());
  if (idBin < 0) {
    Printf("Could not find bin label '%s' in '%s' axis !!!", parameterAxisName.c_str(), currentParameterName.c_str());
    return 5;
  }

  Printf("Axis: %d [parameters] SetRange(%d,%d)", id, idBin, idBin);
  parameterPoint[id] = idBin;
  rh->GetAxis(id)->SetRange(idBin, idBin);



  int nAxisX     = rh->GetNdimensions() - nDimCuts;
  int nAxisY     = rh->GetAxis(0)->GetNbins();
  int pointsSize = cfg["ndmspc"]["result"]["axes"].size() + cfg["ndmspc"]["result"]["data"]["defaults"].size() + 1;
  int points[pointsSize];
  int iPoint       = 0;
  points[iPoint++] = nAxisY;

  int iAxisStart = nDimCuts + 1;
  mapTitle       = currentParameterName + " [";
  json axesArray = cfg["ndmspc"]["result"]["axes"];
  int  idTmp;
  for (int iAxis = iAxisStart; iAxis < rh->GetNdimensions(); iAxis++) {
    idBin = 1;

    if (iAxis - iAxisStart < cfg["ndmspc"]["result"]["axes"].size()) {
      idTmp = iAxis - iAxisStart;
      idBin = cfg["ndmspc"]["result"]["axes"][idTmp]["default"].get<int>() + 1;
    }
    else if (iAxis - iAxisStart - cfg["ndmspc"]["result"]["axes"].size() <
             cfg["ndmspc"]["result"]["data"]["defaults"].size()) {
      idTmp = iAxis - iAxisStart - cfg["ndmspc"]["result"]["axes"].size();
      idBin = cfg["ndmspc"]["result"]["data"]["defaults"][idTmp].get<int>();
    }
    a = (TAxis *)rh->GetAxis(iAxis);
    Printf("Axis: %d [%s] SetRange(%d,%d)", iAxis, a->GetName(), idBin, idBin);
    points[iAxis - nDimCuts] = a->GetNbins();
    parameterPoint[iAxis] = idBin;
    rh->GetAxis(iAxis)->SetRange(idBin, idBin);
    std::string l = a->GetBinLabel(idBin);
    if (l.empty()) {
      mapTitle += std::to_string(a->GetBinLowEdge(idBin));
    }
    else {
      mapTitle += l;
    }
    mapTitle += " ";
  }
  mapTitle[mapTitle.size() - 1] = ']';

  for (auto & p : parameterPoint) {
    printf("%d ", p);
  }
  printf("\n");

  Printf("mapTitle='%s'", mapTitle.c_str());

  auto c2 = new TCanvas("CanvasParams", "CanvasParams", 0, 0, 500, 500);
  c2->HighlightConnect("HighlightParam(TVirtualPad*,TObject*,Int_t,Int_t)");

  // handle systematics
  // Printf("nAxisX=%d nAxisY=%d", nAxisX, nAxisY);

  TH2D * hParamMain = new TH2D("hParamMain", "Param Main", nAxisX, 0, nAxisX, nAxisY, 0, nAxisY);
  for (int i = 0; i < nAxisX; i++) {
    if (i == 0)
    hParamMain->GetXaxis()->SetBinLabel(i + 1, rh->GetAxis(i)->GetName());
    else 
    hParamMain->GetXaxis()->SetBinLabel(i + 1, rh->GetAxis(i+nDimCuts)->GetName());
    for (int j = 0; j < points[i]; j++) {
      hParamMain->SetBinContent(i + 1, j + 1, 1);
    }
  }
  hParamMain->SetStats(0);
  hParamMain->Print();
  hParamMain->Draw("colz");
  hParamMain->SetHighlight();

  // // fIn->Close();

  return 0;
}

void HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  // Printf("param %d %d", xBin, yBin);
  hParamMap = (TH2 *)obj;
  if (!hParamMap) return;
  auto CanvasParam = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasParam");

  if (hParamMap && !hParamMap->IsHighlight()) { // after highlight disabled
    // if (CanvasParam) delete CanvasParam;
    hParamMap->SetTitle("Disable highlight");
    return;
  }

  double binContent = hParamMap->GetBinContent(xBin, yBin);
  if (binContent < 1) return;

  Printf("HighlightParam xBin=%d yBin=%d ", xBin, yBin);

  if (xBin == 1) {
    parameterPoint[xBin - 1] = yBin;
    currentParameterName     = cfg["ndmspc"]["result"]["parameters"]["labels"][yBin - 1].get<std::string>();
    Printf("%s", currentParameterName.c_str());
  }
  else if (xBin > 1) {
    parameterPoint[xBin - 1 + 2] = yBin;
    if (!cfg["ndmspc"]["result"]["axes"][xBin - 2].is_null()) {
      if (!cfg["ndmspc"]["result"]["axes"][xBin - 2]["labels"].is_null()) {
        Printf("%s", cfg["ndmspc"]["result"]["axes"][xBin - 2]["labels"][yBin - 1].get<std::string>().c_str());
      }
      else if (!cfg["ndmspc"]["result"]["axes"][xBin - 2]["ranges"].is_null()) {
        Printf("%s", cfg["ndmspc"]["result"]["axes"][xBin - 2]["ranges"][yBin - 1]["name"].get<std::string>().c_str());
      }
    }
  }

  std::string mytitle = "";
  for (int iDim = 0; iDim < rh->GetNdimensions(); iDim++) {
    if (parameterPoint[iDim] < 0) continue;
    // Printf("HighlightParam rh->GetAxis(%d)->SetRange(%d,%d)", iDim, parameterPoint[iDim], parameterPoint[iDim]);
    rh->GetAxis(iDim)->SetRange(parameterPoint[iDim], parameterPoint[iDim]);
    mytitle += rh->GetAxis(iDim)->GetBinLabel(parameterPoint[iDim]);
    mytitle += " ";
  }
  mytitle += "";

  hParamMap->SetTitle(mytitle.c_str());

  auto CanvasMap = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasMap");
  if (!CanvasMap) {

    CanvasMap = new TCanvas("CanvasMap", "CanvasMap", 505, 0, 600, 500);
    CanvasMap->HighlightConnect("HighlightProj(TVirtualPad*,TObject*,Int_t,Int_t)");
  }

  Printf("HighlightParam xBin=%d yBin=%d nDimCuts=%d", xBin, yBin, nDimCuts);
  Printf("HighlightProj %d %d %s nDimCuts=%d %p", xBin, yBin, currentParameterName.c_str(), nDimCuts, (void *)obj);

  if (nDimCuts == 1) {
    CanvasMap->cd();
    rh->GetAxis(1)->SetRange();
    hMap = rh->Projection(1, "O");
    // std::string htitle = hMap->GetTitle();
    // htitle += mapTitle;
    // hMap->SetTitle(htitle.c_str());
    hMap->SetTitle(mytitle.c_str());
    hMap->SetStats(0);
    hMap->Draw();
    hMap->SetHighlight();
  }
  else if (nDimCuts == 2) {
    CanvasMap->cd();

    // parameterPoint[iDim]
    rh->GetAxis(1)->SetRange();
    rh->GetAxis(2)->SetRange();
    hMap = rh->Projection(2, 1, "O");
    // std::string htitle = hMap->GetTitle();
    // htitle += mapTitle;
    // hMap->SetTitle(htitle.c_str());
    hMap->SetTitle(mytitle.c_str());
    hMap->SetStats(0);
    hMap->Draw("colz");
    hMap->SetHighlight();
  }

  CanvasMap->Modified();
  CanvasMap->Update();
}

void HighlightProj(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  auto  h1 = dynamic_cast<TH1D *>(obj);
  auto  h2 = dynamic_cast<TH2D *>(obj);
  TH1 * px = nullptr;
  TH1 * py = nullptr;

  if (!h1 && !h2) return;
  auto CanvasProj = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProj");

  if (h2 && !h2->IsHighlight()) { // after highlight disabled
    // if (CanvasProj) delete CanvasProj;
    h2->SetTitle("Disable highlight");
    return;
  }
  if (h1 && !h1->IsHighlight()) { // after highlight disabled
    // if (CanvasProj) delete CanvasProj;
    h1->SetTitle("Disable highlight");
    return;
  }

  Printf("HighlightProj %d %d %s %p", xBin, yBin, currentParameterName.c_str(), (void *)obj);
  //  if (info) info->SetTitle("");

  if (h1) {
    rh->GetAxis(1)->SetRange(0, -1);
    px = rh->Projection(1);
    px->SetTitle(TString::Format("ProjectionY of binx[%02d]", xBin));
    // px->SetMaximum(1.025);
    // px->SetMaximum(1.025);
  }
  else if (h2) {

    parameterPoint[1]   = -xBin;
    parameterPoint[2]   = -yBin;
    std::string mytitle = "[";
    for (int iDim = 0; iDim < rh->GetNdimensions(); iDim++) {
      if (parameterPoint[iDim] < 0) continue;
      // Printf("HighlightProj SetRange %d %d %d", iDim, parameterPoint[iDim], parameterPoint[iDim]);
      rh->GetAxis(iDim)->SetRange(parameterPoint[iDim], parameterPoint[iDim]);
      mytitle += rh->GetAxis(iDim)->GetBinLabel(parameterPoint[iDim]);
      mytitle += " ";
    }
    mytitle += "]";

    // Printf("[x] HighlightProj SetRange axes %d %d %d", 2, yBin, yBin);
    // Printf("[x] HighlightProj SetRange axes reset %d %d %d", 1, -1, -1);
    rh->GetAxis(1)->SetRange();
    rh->GetAxis(2)->SetRange(yBin, yBin);
    px = rh->Projection(1);
    // px->Print("all");
    px->SetTitle(TString::Format("%s %s %s [%f,%f]", rh->GetAxis(1)->GetName(), mytitle.c_str(),
                                 rh->GetAxis(2)->GetName(), h2->GetYaxis()->GetBinLowEdge(yBin),
                                 h2->GetYaxis()->GetBinUpEdge(yBin)));

    // Printf("[y] HighlightProj SetRange axes %d %d %d", 1, xBin, xBin);
    // Printf("[y] HighlightProj SetRange axes reset %d %d %d", 2, -1, -1);

    rh->GetAxis(2)->SetRange();
    rh->GetAxis(1)->SetRange(xBin, xBin);
    py = rh->Projection(2);
    // py->Print("all");
    py->SetTitle(TString::Format("%s %s %s [%f,%f]", rh->GetAxis(2)->GetName(), mytitle.c_str(),
                                 rh->GetAxis(1)->GetName(), h2->GetXaxis()->GetBinLowEdge(xBin),
                                 h2->GetXaxis()->GetBinUpEdge(xBin)));

    // Printf("%s %s", currentParameterName.c_str(), cfg["ndmspc"]["result"]["parameters"]["draw"].dump().c_str());

    if (!cfg["ndmspc"]["result"]["parameters"]["draw"][currentParameterName].is_null()) {
      Printf("Apply %s %s", currentParameterName.c_str(),
             cfg["ndmspc"]["result"]["parameters"]["draw"][currentParameterName].dump().c_str());
      double min = cfg["ndmspc"]["result"]["parameters"]["draw"][currentParameterName]["min"].get<double>();
      double max = cfg["ndmspc"]["result"]["parameters"]["draw"][currentParameterName]["max"].get<double>();
      px->SetMinimum(min);
      px->SetMaximum(max);
      py->SetMinimum(min);
      py->SetMaximum(max);
    }
    // px->SetMinimum(1.015);
    // px->SetMaximum(1.025);
    // py->SetMinimum(1.015);
    // py->SetMaximum(1.025);
  }

  // Printf("%p %p", (void *)px, (void *)py);

  if (!CanvasProj) {
    CanvasProj = new TCanvas("CanvasProj", "CanvasProj", 505, 505 + 70, 600, 600);
    // CanvasProj->Divide(1, 1);
    if (h1) CanvasProj->Divide(1, 1);
    if (h2) CanvasProj->Divide(1, 2);
    CanvasProj->HighlightConnect("HighlightPoint(TVirtualPad*,TObject*,Int_t,Int_t)");
  }

  CanvasProj->cd(1);
  px->Draw();
  px->SetHighlight();
  if (py) {
    CanvasProj->cd(2);
    py->Draw();
    py->SetHighlight();
  }
  if (h1) h1->SetTitle(TString::Format("Highlight bin [%02d, %02d]", xBin, yBin).Data());
  if (h2)
    h2->SetTitle(TString::Format("Highlight bin %s [%0.2f, %0.2f], %s [%0.2f, %0.2f]", rh->GetAxis(1)->GetName(),
                                 h2->GetXaxis()->GetBinLowEdge(xBin), h2->GetXaxis()->GetBinUpEdge(xBin),
                                 rh->GetAxis(2)->GetName(), h2->GetYaxis()->GetBinLowEdge(yBin),
                                 h2->GetYaxis()->GetBinUpEdge(yBin))
                     .Data());
  pad->Modified();
  pad->Update();

  CanvasProj->GetPad(1)->Modified();
  if (py) CanvasProj->GetPad(2)->Modified();
  CanvasProj->Update();
}

void HighlightPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  Printf("HighlightPoint %d %d %s %p", xBin, yBin, currentParameterName.c_str(), (void *)obj);
  auto h = (TH1 *)obj;

  if (!h) return;

  auto CanvasContent = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasContent");

  TString pName = h->GetName();

  if (pName.Contains("proj_1")) {
    // Printf("1");
    parameterPoint[1] = -xBin;
  }
  else if (pName.Contains("proj_2")) {
    // Printf("2");
    parameterPoint[2] = -xBin;
  }

  std::string path;
  for (auto & p : parameterPoint) {
    if (path.empty()) {
      path = "content/";
      continue;
    }
    // printf("%d ", p);
    path += std::to_string(std::abs(p)) + "/";
  }
  // printf("\n");
  path += "hPeak";
  // Printf("XXXXXXXX %s", path.c_str());
  // path[path.size() - 1] = '/';

  if (!CanvasContent) {
    CanvasContent = new TCanvas("CanvasContent", "CanvasContent", 1200, 0, 600, 600);
    // CanvasContent->Divide(1, 1);
  }

  TH1 * hh = (TH1 *)fIn->Get(path.c_str());
  if (hh) {
    CanvasContent->cd();
    // hh->Print();
    hh->Draw();
  }
  else {
    if (CanvasContent) delete CanvasContent;
    return;
    // CanvasContent->Clear();
  }

  CanvasContent->Modified();
  CanvasContent->Update();

  // h->Print();
}
