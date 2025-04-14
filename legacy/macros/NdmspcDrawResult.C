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
std::string      _currentParameterName;
std::string      _currentContentPath;
std::vector<int> _parameterPoint;
std::vector<int> _projectionAxes;
TH1 *            hMap         = nullptr;
TH1 *            _mapAxesType = nullptr;
std::string      mapTitle;
TH1 *            hParamMap = nullptr;
int              nDimCuts  = 0;
void             HighlightProj(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
void             HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
void             HighlightData(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
void             HighlightPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin);
void             UpdateRanges();
void             DrawProjections();
void             DrawUser();
int              NdmspcDrawResult(std::string configFile = "myAnalysis.json", std::string parameter = "",
                                  std::string resultsHnSparseName = "results", std::string parameterAxisName = "parameters",
                                  std::string inputFile = "")
{

  _currentParameterName = parameter;

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

  _mapAxesType = (TH1 *)fIn->Get("mapAxesType");
  if (!_mapAxesType) {
    Printf("Error: 'mapAxesType' histogram was not found !!!");
    return 4;
  }

  for (int iDim = 0; iDim < rh->GetNdimensions(); iDim++) _parameterPoint.push_back(-1);

  if (_currentParameterName.empty()) {
    int idxDefault        = cfg["ndmspc"]["result"]["parameters"]["default"].get<int>();
    _currentParameterName = cfg["ndmspc"]["result"]["parameters"]["labels"][idxDefault].get<std::string>();
  }
  Printf("Paremeter: %s", _currentParameterName.c_str());

  TAxis * a = (TAxis *)rh->GetListOfAxes()->FindObject(parameterAxisName.c_str());
  if (a == nullptr) {
    return 5;
  }
  Int_t id    = rh->GetListOfAxes()->IndexOf(a);
  Int_t idBin = a->FindBin(_currentParameterName.c_str());
  if (idBin < 0) {
    Printf("Could not find bin label '%s' in '%s' axis !!!", parameterAxisName.c_str(), _currentParameterName.c_str());
    return 6;
  }

  Printf("Axis: %d [parameters] SetRange(%d,%d)", id, idBin, idBin);
  _parameterPoint[id] = idBin;
  rh->GetAxis(id)->SetRange(idBin, idBin);

  int nAxisX     = rh->GetNdimensions();
  int nAxisY     = rh->GetAxis(0)->GetNbins();
  int pointsSize = cfg["ndmspc"]["result"]["axes"].size() + cfg["ndmspc"]["result"]["data"]["defaults"].size() + 1;
  int points[pointsSize];
  int iPoint       = 0;
  points[iPoint++] = nAxisY;

  int iAxisStart = 1;
  mapTitle       = _currentParameterName + " [";
  json axesArray = cfg["ndmspc"]["result"]["axes"];
  int  idTmp;
  bool isDataSys = true;
  bool hasDataMc = false;
  for (int iAxis = iAxisStart; iAxis < rh->GetNdimensions(); iAxis++) {
    idBin                = 1;
    std::string axisType = _mapAxesType->GetXaxis()->GetBinLabel(iAxis + 1);
    if (!hasDataMc) hasDataMc = !axisType.compare("data");
    if (!axisType.compare("proj")) {
      isDataSys = false;
      _projectionAxes.push_back(iAxis);
    }
    else if (!axisType.compare("sys") || !axisType.compare("data")) {
      if (isDataSys) {
        idTmp = iAxis - iAxisStart;
        idBin = cfg["ndmspc"]["result"]["data"]["defaults"][idTmp].get<int>();
      }
      else {
        idTmp = iAxis - iAxisStart - cfg["ndmspc"]["result"]["data"]["defaults"].size() - nDimCuts;
        idBin = axesArray[idTmp]["default"].get<int>() + 1;
      }
    }
    a = (TAxis *)rh->GetAxis(iAxis);
    Printf("Axis: %d [%s][%s] SetRange(%d,%d)", iAxis, axisType.c_str(), a->GetName(), idBin, idBin);
    points[iAxis]          = a->GetNbins();
    _parameterPoint[iAxis] = idBin;
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

  for (auto & p : _parameterPoint) {
    printf("%d ", p);
  }
  printf("\n");

  Printf("mapTitle='%s'", mapTitle.c_str());

  auto CanvasMain = new TCanvas("CanvasMain", "CanvasMain", 0, 0, 500, 500);
  CanvasMain->Divide(1, 3);
  CanvasMain->HighlightConnect("HighlightMain(TVirtualPad*,TObject*,Int_t,Int_t)");

  //   // handle systematics
  //   // Printf("nAxisX=%d nAxisY=%d", nAxisX, nAxisY);
  CanvasMain->cd(1);
  TH1 * hParamMain = new TH1S("hParamMain", "Param Main", rh->GetAxis(0)->GetNbins(), 0, rh->GetAxis(0)->GetNbins());

  for (int i = 0; i < rh->GetAxis(0)->GetNbins(); i++) {
    hParamMain->GetXaxis()->SetBinLabel(i + 1, rh->GetAxis(0)->GetBinLabel(i + 1));
    // hParamMain->SetBinContent(i + 1, 1, 1);
  }

  // for (int i = 0; i < nAxisX; i++) {
  //   hParamMain->GetXaxis()->SetBinLabel(i + 1, rh->GetAxis(i)->GetName());
  //   for (int j = 0; j < points[i]; j++) {
  //     hParamMain->SetBinContent(i + 1, j + 1, 1);
  //   }
  // }
  hParamMain->SetStats(0);
  hParamMain->SetLabelSize(0.07);
  //   hParamMain->Print();
  hParamMain->Draw();
  hParamMain->SetHighlight();

  CanvasMain->cd(2);
  if (hasDataMc) {
    rh->GetAxis(1)->SetRange();
    rh->GetAxis(2)->SetRange();
    TH2 * hDataMc = rh->Projection(2, 1, "O");

    for (int i = 0; i < hDataMc->GetNbinsX() + 1; i++)
      for (int j = 0; j < hDataMc->GetNbinsY() + 1; j++) {
        if (hDataMc->GetBinContent(i, j) > 0) hDataMc->SetBinContent(i, j, 1);
      }

    hDataMc->SetNameTitle("hDataMc", "Data vs. MC");
    hDataMc->SetStats(0);
    for (int i = 0; i < rh->GetAxis(1)->GetNbins(); i++) {
      hDataMc->GetXaxis()->SetBinLabel(i + 1, rh->GetAxis(1)->GetBinLabel(i + 1));
      // hParamMain->SetBinContent(i + 1, 1, 1);
    }
    for (int i = 0; i < rh->GetAxis(2)->GetNbins(); i++) {
      hDataMc->GetYaxis()->SetBinLabel(i + 1, rh->GetAxis(2)->GetBinLabel(i + 1));
      // hParamMain->SetBinContent(i + 1, 1, 1);
    }

    // hDataMc->Reset();
    // hParamMain->SetBinContent(i + 1, j + 1, 1);

    hDataMc->Draw("col");
    hDataMc->SetHighlight();
  }

  DrawProjections();

  //   // // fIn->Close();

  return 0;
}

void DrawUser()
{
  Printf("DrawUser : Getting '%s' ...", _currentContentPath.c_str());
  auto CanvasUser = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasUser");
  if (!CanvasUser) {
    CanvasUser = new TCanvas("CanvasUser", "CanvasUser", 1110, 0, 600, 500);
    // Printf("CanvasUser");
    // CanvasUser->Divide(1, _projectionAxes.size());
    // CanvasUser->HighlightConnect("HighlightProj(TVirtualPad*,TObject*,Int_t,Int_t)");
  }
  TH1 * hPeak = (TH1 *)fIn->Get(_currentContentPath.c_str());
  if (!hPeak) {
    delete CanvasUser;
    return;
  }

  CanvasUser->cd();
  // hPeak->Print("all");
  hPeak->Draw();
  CanvasUser->Modified();
  CanvasUser->Update();
}

void UpdateRanges()
{
  for (int iAxis = 0; iAxis < rh->GetNdimensions(); iAxis++) {
    Printf("Axis: %d [%s] SetRange(%d,%d)", iAxis, rh->GetAxis(iAxis)->GetName(), _parameterPoint[iAxis],
           _parameterPoint[iAxis]);
    rh->GetAxis(iAxis)->SetRange(_parameterPoint[iAxis], _parameterPoint[iAxis]);
  }

  _currentContentPath.clear();
  int i = 0;
  for (auto & p : _parameterPoint) {
    if (_currentContentPath.empty()) {
      _currentContentPath = "content/";
      continue;
    }
    _currentContentPath += std::to_string(std::abs(p)) + "/";
  }
  _currentContentPath += "hPeak";
  // Printf("Content _currentContentPath: %s", _currentContentPath.c_str());
}

void DrawProjections()
{

  Printf("DrawProjections %zu", _projectionAxes.size());
  UpdateRanges();

  int    xBin = _parameterPoint[_projectionAxes[0]];
  int    yBin = _parameterPoint[_projectionAxes[1]];
  double min  = 1;
  double max  = 0;

  if (!cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName].is_null()) {
    Printf("Apply %s %s", _currentParameterName.c_str(),
           cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName].dump().c_str());
    min = cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName]["min"].get<double>();
    max = cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName]["max"].get<double>();
  }

  // DrawUser();

  auto CanvasProjectionMap = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProjectionMap");
  if (!CanvasProjectionMap) {
    CanvasProjectionMap = new TCanvas("CanvasProjectionMap", "CanvasProjectionMap", 505, 0, 600, 500);
    Printf("CanvasProjectionMap xBin=%d yBin=%d %zu", xBin, yBin, _projectionAxes.size());
    CanvasProjectionMap->HighlightConnect("HighlightProjectionPoint(TVirtualPad*,TObject*,Int_t,Int_t)");
  }

  if (_projectionAxes.size() == 1) {
    CanvasProjectionMap->cd();
    rh->GetAxis(_projectionAxes[0])->SetRange();
    TH1 * h = rh->Projection(_projectionAxes[0], "O");
    h->SetHighlight();
    if (min < max) {
      h->SetMinimum(min);
      h->SetMaximum(max);
    }
    h->Draw();

    CanvasProjectionMap->Modified();
    CanvasProjectionMap->Update();
    return;
  }
  if (_projectionAxes.size() == 2) {
    CanvasProjectionMap->cd();
    rh->GetAxis(_projectionAxes[0])->SetRange();
    rh->GetAxis(_projectionAxes[1])->SetRange();
    rh->Projection(_projectionAxes[1], _projectionAxes[0], "O")->Draw("colz");
    TH2 * h = rh->Projection(_projectionAxes[1], _projectionAxes[0], "O");
    h->SetHighlight();
    h->Draw("colz");
    CanvasProjectionMap->Modified();
    CanvasProjectionMap->Update();
  }

  auto CanvasProjections = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProjections");
  if (!CanvasProjections) {
    CanvasProjections = new TCanvas("CanvasProjections", "CanvasProjections", 505, 700, 600, 500);
    Printf("CanvasProjections xBin=%d yBin=%d %zu", xBin, yBin, _projectionAxes.size());
    CanvasProjections->Divide(1, _projectionAxes.size());
    // CanvasProjections->HighlightConnect("HighlightProj(TVirtualPad*,TObject*,Int_t,Int_t)");
  }

  TH1 * px = nullptr;
  TH1 * py = nullptr;
  CanvasProjections->cd(1);
  rh->GetAxis(_projectionAxes[0])->SetRange();
  rh->GetAxis(_projectionAxes[1])->SetRange(xBin, xBin);
  px = rh->Projection(_projectionAxes[0], "O");
  if (px) {
    if (min < max) {
      px->SetMinimum(min);
      px->SetMaximum(max);
    }
    px->Draw();
  }
  if (_projectionAxes.size() == 2) {
    CanvasProjections->cd(2);
    rh->GetAxis(_projectionAxes[1])->SetRange();
    rh->GetAxis(_projectionAxes[0])->SetRange(yBin, yBin);
    py = rh->Projection(_projectionAxes[1], "O");
    if (py) {
      if (min < max) {
        py->SetMinimum(min);
        py->SetMaximum(max);
      }
      py->Draw();
    }
  }

  CanvasProjections->Modified();
  CanvasProjections->Update();
}

void HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  auto        h     = (TH1 *)obj;
  std::string hName = h->GetName();
  if (!hName.compare("hParamMain")) {
    HighlightParam(pad, obj, xBin, yBin);
  }
  else if (!hName.compare("hDataMc")) {
    HighlightData(pad, obj, xBin, yBin);
  }
}

void HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  Printf("HighlightParam %d %d", xBin, yBin);
  TH1 * hParamMap = (TH1 *)obj;
  if (!hParamMap) return;

  // double binContent = hParamMap->GetBinContent(xBin, yBin);
  // if (binContent <= 0) return;
  hParamMap->SetTitle(hParamMap->GetXaxis()->GetBinLabel(xBin));
  _parameterPoint[0]    = xBin;
  _currentParameterName = hParamMap->GetXaxis()->GetBinLabel(xBin);
  Printf("%s", _currentParameterName.c_str());
  DrawProjections();

  // UpdateRanges();
  // rh->GetAxis(0)->SetRange(xBin, xBin);

  // auto CanvasParam = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasParam");

  // if (hParamMap && !hParamMap->IsHighlight()) { // after highlight disabled
  //   // if (CanvasParam) delete CanvasParam;
  //   hParamMap->SetTitle("Disable highlight");
  //   return;
  // }

  // Printf("HighlightParam xBin=%d yBin=%d ", xBin, yBin);
  // hParamMap->SetTitle(hParamMap->GetXaxis()->GetBinLabel(xBin));
  // _parameterPoint[0]    = xBin;
  // _currentParameterName = cfg["ndmspc"]["result"]["parameters"]["labels"][xBin - 1].get<std::string>();
  // Printf("%s", _currentParameterName.c_str());
  // if (xBin == 1) {

  // }
  //   else if (xBin > 1) {
  //     _parameterPoint[xBin - 1 + 2] = yBin;
  //     if (!cfg["ndmspc"]["result"]["axes"][xBin - 2].is_null()) {
  //       if (!cfg["ndmspc"]["result"]["axes"][xBin - 2]["labels"].is_null()) {
  //         Printf("%s", cfg["ndmspc"]["result"]["axes"][xBin - 2]["labels"][yBin - 1].get<std::string>().c_str());
  //       }
  //       else if (!cfg["ndmspc"]["result"]["axes"][xBin - 2]["ranges"].is_null()) {
  //         Printf("%s", cfg["ndmspc"]["result"]["axes"][xBin - 2]["ranges"][yBin -
  //         1]["name"].get<std::string>().c_str());
  //       }
  //     }
  //   }

  // std::string mytitle = "";
  // for (int iDim = 0; iDim < rh->GetNdimensions(); iDim++) {
  //   if (_parameterPoint[iDim] < 0) continue;
  //   // Printf("HighlightParam rh->GetAxis(%d)->SetRange(%d,%d)", iDim, _parameterPoint[iDim], _parameterPoint[iDim]);
  //   rh->GetAxis(iDim)->SetRange(_parameterPoint[iDim], _parameterPoint[iDim]);
  //   std::string binLabel = rh->GetAxis(iDim)->GetBinLabel(_parameterPoint[iDim]);
  //   if (!binLabel.empty())
  //     mytitle += binLabel;
  //   else
  //     mytitle += TString::Format("%.2f", rh->GetAxis(iDim)->GetBinLowEdge(_parameterPoint[iDim])).Data();
  //   mytitle += " ";
  // }
  // mytitle += "";

  // hParamMap->SetTitle(mytitle.c_str());

  //   auto CanvasMap = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasMap");
  //   if (!CanvasMap) {

  //     CanvasMap = new TCanvas("CanvasMap", "CanvasMap", 505, 0, 600, 500);
  //     CanvasMap->HighlightConnect("HighlightProj(TVirtualPad*,TObject*,Int_t,Int_t)");
  //   }

  //   Printf("HighlightParam xBin=%d yBin=%d nDimCuts=%d", xBin, yBin, nDimCuts);
  //   Printf("HighlightProj %d %d %s nDimCuts=%d %p", xBin, yBin, _currentParameterName.c_str(), nDimCuts, (void
  //   *)obj);

  //   if (nDimCuts == 1) {
  //     CanvasMap->cd();
  //     rh->GetAxis(1)->SetRange();
  //     hMap = rh->Projection(1, "O");
  //     // std::string htitle = hMap->GetTitle();
  //     // htitle += mapTitle;
  //     // hMap->SetTitle(htitle.c_str());
  //     hMap->SetTitle(mytitle.c_str());
  //     hMap->SetStats(0);
  //     hMap->Draw();
  //     hMap->SetHighlight();
  //   }
  //   else if (nDimCuts == 2) {
  //     CanvasMap->cd();

  //     // _parameterPoint[iDim]
  //     rh->GetAxis(1)->SetRange();
  //     rh->GetAxis(2)->SetRange();
  //     hMap      = rh->Projection(2, 1, "O");
  //     TAxis * a = rh->GetAxis(1);
  //     for (int iLablel = 0; iLablel < a->GetNbins(); iLablel++) {
  //       hMap->GetXaxis()->SetBinLabel(iLablel + 1, a->GetBinLabel(iLablel + 1));
  //     }
  //     a = rh->GetAxis(2);
  //     for (int iLablel = 0; iLablel < a->GetNbins(); iLablel++) {
  //       hMap->GetYaxis()->SetBinLabel(iLablel + 1, a->GetBinLabel(iLablel + 1));
  //     }

  //     // std::string htitle = hMap->GetTitle();
  //     // htitle += mapTitle;
  //     // hMap->SetTitle(htitle.c_str());
  //     hMap->SetTitle(mytitle.c_str());
  //     hMap->SetStats(0);
  //     hMap->Draw("colz");
  //     hMap->SetHighlight();
  //   }

  pad->Modified();
  pad->Update();
}

void HighlightData(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  // Printf("HighlightData %d %d", xBin, yBin);
  TH2 * hDataMc = (TH2 *)obj;
  if (!hDataMc) return;

  if (hDataMc->GetBinContent(xBin, yBin) <= 0) return;

  std::string data = hDataMc->GetXaxis()->GetBinLabel(xBin);
  std::string mc   = hDataMc->GetYaxis()->GetBinLabel(yBin);
  Printf("data=%s[%d] mc=%s[%d]", data.c_str(), xBin, mc.c_str(), yBin);

  _parameterPoint[1] = xBin;
  _parameterPoint[2] = yBin;

  DrawProjections();
  DrawUser();
}

void HighlightProjectionPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  Printf("HighlightProjectionPoint %d %d %d", xBin, yBin, _projectionAxes[0]);
  if (_projectionAxes.size() == 1) {
    _parameterPoint[_projectionAxes[0]] = xBin;
  }
  else if (_projectionAxes.size() == 2) {
    _parameterPoint[_projectionAxes[0]] = xBin;
    _parameterPoint[_projectionAxes[1]] = yBin;
  }
  UpdateRanges();
  DrawUser();
}

// void HighlightProj(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
// {
//   auto  h1 = dynamic_cast<TH1D *>(obj);
//   auto  h2 = dynamic_cast<TH2D *>(obj);
//   TH1 * px = nullptr;
//   TH1 * py = nullptr;

//   if (!h1 && !h2) return;
//   auto CanvasProj = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProj");

//   if (h2 && !h2->IsHighlight()) { // after highlight disabled
//     // if (CanvasProj) delete CanvasProj;
//     h2->SetTitle("Disable highlight");
//     return;
//   }
//   if (h1 && !h1->IsHighlight()) { // after highlight disabled
//     // if (CanvasProj) delete CanvasProj;
//     h1->SetTitle("Disable highlight");
//     return;
//   }

//   Printf("HighlightProj %d %d %s %p", xBin, yBin, _currentParameterName.c_str(), (void *)obj);
//   //  if (info) info->SetTitle("");

//   if (h1) {
//     rh->GetAxis(1)->SetRange(0, -1);
//     px = rh->Projection(1);
//     px->SetTitle(TString::Format("ProjectionY of binx[%02d]", xBin));
//     // px->SetMaximum(1.025);
//     // px->SetMaximum(1.025);
//   }
//   else if (h2) {

//     _parameterPoint[1]   = -xBin;
//     _parameterPoint[2]   = -yBin;
//     std::string mytitle = "[";
//     for (int iDim = 0; iDim < rh->GetNdimensions(); iDim++) {
//       if (_parameterPoint[iDim] < 0) continue;
//       // Printf("HighlightProj SetRange %d %d %d", iDim, _parameterPoint[iDim], _parameterPoint[iDim]);
//       rh->GetAxis(iDim)->SetRange(_parameterPoint[iDim], _parameterPoint[iDim]);
//       mytitle += rh->GetAxis(iDim)->GetBinLabel(_parameterPoint[iDim]);
//       mytitle += " ";
//     }
//     mytitle += "]";

//     // Printf("[x] HighlightProj SetRange axes %d %d %d", 2, yBin, yBin);
//     // Printf("[x] HighlightProj SetRange axes reset %d %d %d", 1, -1, -1);
//     rh->GetAxis(1)->SetRange();
//     rh->GetAxis(2)->SetRange(yBin, yBin);
//     px = rh->Projection(1);
//     // px->Print("all");
//     px->SetTitle(TString::Format("%s %s %s [%f,%f]", rh->GetAxis(1)->GetName(), mytitle.c_str(),
//                                  rh->GetAxis(2)->GetName(), h2->GetYaxis()->GetBinLowEdge(yBin),
//                                  h2->GetYaxis()->GetBinUpEdge(yBin)));

//     // Printf("[y] HighlightProj SetRange axes %d %d %d", 1, xBin, xBin);
//     // Printf("[y] HighlightProj SetRange axes reset %d %d %d", 2, -1, -1);

//     rh->GetAxis(2)->SetRange();
//     rh->GetAxis(1)->SetRange(xBin, xBin);
//     py = rh->Projection(2);
//     // py->Print("all");
//     py->SetTitle(TString::Format("%s %s %s [%f,%f]", rh->GetAxis(2)->GetName(), mytitle.c_str(),
//                                  rh->GetAxis(1)->GetName(), h2->GetXaxis()->GetBinLowEdge(xBin),
//                                  h2->GetXaxis()->GetBinUpEdge(xBin)));

//     // Printf("%s %s", _currentParameterName.c_str(), cfg["ndmspc"]["result"]["parameters"]["draw"].dump().c_str());

//     if (!cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName].is_null()) {
//       Printf("Apply %s %s", _currentParameterName.c_str(),
//              cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName].dump().c_str());
//       double min = cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName]["min"].get<double>();
//       double max = cfg["ndmspc"]["result"]["parameters"]["draw"][_currentParameterName]["max"].get<double>();
//       px->SetMinimum(min);
//       px->SetMaximum(max);
//       py->SetMinimum(min);
//       py->SetMaximum(max);
//     }
//     // px->SetMinimum(1.015);
//     // px->SetMaximum(1.025);
//     // py->SetMinimum(1.015);
//     // py->SetMaximum(1.025);
//   }

//   // Printf("%p %p", (void *)px, (void *)py);

//   if (!CanvasProj) {
//     CanvasProj = new TCanvas("CanvasProj", "CanvasProj", 505, 505 + 70, 600, 600);
//     // CanvasProj->Divide(1, 1);
//     if (h1) CanvasProj->Divide(1, 1);
//     if (h2) CanvasProj->Divide(1, 2);
//     CanvasProj->HighlightConnect("HighlightPoint(TVirtualPad*,TObject*,Int_t,Int_t)");
//   }

//   CanvasProj->cd(1);
//   px->Draw();
//   px->SetHighlight();
//   if (py) {
//     CanvasProj->cd(2);
//     py->Draw();
//     py->SetHighlight();
//   }
//   if (h1) h1->SetTitle(TString::Format("Highlight bin [%02d, %02d]", xBin, yBin).Data());
//   if (h2)
//     h2->SetTitle(TString::Format("Highlight bin %s [%0.2f, %0.2f], %s [%0.2f, %0.2f]", rh->GetAxis(1)->GetName(),
//                                  h2->GetXaxis()->GetBinLowEdge(xBin), h2->GetXaxis()->GetBinUpEdge(xBin),
//                                  rh->GetAxis(2)->GetName(), h2->GetYaxis()->GetBinLowEdge(yBin),
//                                  h2->GetYaxis()->GetBinUpEdge(yBin))
//                      .Data());
//   pad->Modified();
//   pad->Update();

//   CanvasProj->GetPad(1)->Modified();
//   if (py) CanvasProj->GetPad(2)->Modified();
//   CanvasProj->Update();
// }

// void HighlightPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
// {
//   Printf("HighlightPoint %d %d %s %p", xBin, yBin, _currentParameterName.c_str(), (void *)obj);
//   auto h = (TH1 *)obj;

//   if (!h) return;

//   auto CanvasContent = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasContent");

//   TString pName = h->GetName();

//   if (pName.Contains("proj_1")) {
//     // Printf("1");
//     _parameterPoint[1] = -xBin;
//   }
//   else if (pName.Contains("proj_2")) {
//     // Printf("2");
//     _parameterPoint[2] = -xBin;
//   }

//   std::string path;
//   for (auto & p : _parameterPoint) {
//     if (path.empty()) {
//       path = "content/";
//       continue;
//     }
//     // printf("%d ", p);
//     path += std::to_string(std::abs(p)) + "/";
//   }
//   // printf("\n");
//   path += "hPeak";
//   // Printf("XXXXXXXX %s", path.c_str());
//   // path[path.size() - 1] = '/';

//   if (!CanvasContent) {
//     CanvasContent = new TCanvas("CanvasContent", "CanvasContent", 1200, 0, 600, 600);
//     // CanvasContent->Divide(1, 1);
//   }

//   TH1 * hh = (TH1 *)fIn->Get(path.c_str());
//   if (hh) {
//     CanvasContent->cd();
//     // hh->Print();
//     hh->Draw();
//   }
//   else {
//     if (CanvasContent) delete CanvasContent;
//     return;
//     // CanvasContent->Clear();
//   }

//   CanvasContent->Modified();
//   CanvasContent->Update();

//   // h->Print();
// }
