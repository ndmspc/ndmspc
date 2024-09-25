#include <TString.h>
#include <TApplication.h>
#include <TCanvas.h>
#include <TRootCanvas.h>
#include <TROOT.h>
#include <TH2.h>
#include <TH1D.h>
#include <TCanvas.h>
#include "PointDraw.h"
#include "Utils.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::PointDraw);
/// \endcond

namespace NdmSpc {
PointDraw::PointDraw() : TObject()

{
  ///
  /// Default constructor
  ///
}

PointDraw::~PointDraw()
{
  ///
  /// Descructor
  ///
}

int PointDraw::Draw(std::string config, std::string userConfig)
{
  ///
  /// Draw
  ///
  TApplication app("app", nullptr, nullptr);

  std::string parameter           = "";
  std::string resultsHnSparseName = "results";
  std::string parameterAxisName   = "parameters";
  std::string inputFile;

  fCurrentParameterName = parameter;

  TH1::AddDirectory(kFALSE);

  json fCfg = Utils::LoadConfig(config, userConfig);

  bool histogramEnabled = false;
  if (!fCfg["ndmspc"]["data"]["histogram"]["enabled"].is_null() ||
      fCfg["ndmspc"]["data"]["histogram"]["enabled"].is_boolean())
    histogramEnabled = fCfg["ndmspc"]["data"]["histogram"]["enabled"].get<bool>();

  std::string hostUrl = fCfg["ndmspc"]["output"]["host"].get<std::string>();
  if (hostUrl.empty()) {
    Printf("Error:  fCfg[ndmspc][output][host] is empty!!!");
    return 2;
  }

  std::string path = hostUrl + "/" + fCfg["ndmspc"]["output"]["dir"].get<std::string>() + "/";

  for (auto & cut : fCfg["ndmspc"]["cuts"]) {
    if (cut["enabled"].is_boolean() && cut["enabled"].get<bool>() == false) continue;
    path += cut["axis"].get<std::string>() + "_";
    fNDimCuts++;
  }

  path[path.size() - 1] = '/';

  if (inputFile.empty()) inputFile = path + "results.root";

  Printf("Opening file '%s' ...", inputFile.c_str());

  fIn = TFile::Open(inputFile.c_str());
  if (!fIn) {
    Printf("Error: Input file '%s' was not found !!!", inputFile.c_str());
    return 2;
  }

  fResultHnSparse = (THnSparse *)fIn->Get(resultsHnSparseName.c_str());

  if (!fResultHnSparse) {
    Printf("Error: Results THnSparse histogram '%s' was not found !!!", resultsHnSparseName.c_str());
    return 3;
  }

  fMapAxesType = (TH1 *)fIn->Get("mapAxesType");
  if (!fMapAxesType) {
    Printf("Error: 'mapAxesType' histogram was not found !!!");
    return 4;
  }
  for (int iDim = 0; iDim < fResultHnSparse->GetNdimensions(); iDim++) fParameterPoint.push_back(-1);

  if (fCurrentParameterName.empty()) {
    int idxDefault        = fCfg["ndmspc"]["result"]["parameters"]["default"].get<int>();
    fCurrentParameterName = fCfg["ndmspc"]["result"]["parameters"]["labels"][idxDefault].get<std::string>();
  }
  Printf("Paremeter: %s", fCurrentParameterName.c_str());

  TAxis * a = (TAxis *)fResultHnSparse->GetListOfAxes()->FindObject(parameterAxisName.c_str());
  if (a == nullptr) {
    return 5;
  }
  Int_t id    = fResultHnSparse->GetListOfAxes()->IndexOf(a);
  Int_t idBin = a->FindBin(fCurrentParameterName.c_str());
  if (idBin < 0) {
    Printf("Could not find bin label '%s' in '%s' axis !!!", parameterAxisName.c_str(), fCurrentParameterName.c_str());
    return 6;
  }

  Printf("Axis: %d [parameters] SetRange(%d,%d)", id, idBin, idBin);
  fParameterPoint[id] = idBin;
  fResultHnSparse->GetAxis(id)->SetRange(idBin, idBin);

  int nAxisX     = fResultHnSparse->GetNdimensions();
  int nAxisY     = fResultHnSparse->GetAxis(0)->GetNbins();
  int pointsSize = fCfg["ndmspc"]["result"]["axes"].size() + 1;
  if (histogramEnabled) pointsSize += fCfg["ndmspc"]["result"]["data"]["defaults"].size() + 1;
  int points[pointsSize];
  int iPoint       = 0;
  points[iPoint++] = nAxisY;

  int iAxisStart = 1;
  fMapTitle      = fCurrentParameterName + " [";
  json axesArray = fCfg["ndmspc"]["result"]["axes"];
  int  idTmp;
  bool isDataSys = false;
  bool hasDataMc = false;
  for (int iAxis = iAxisStart; iAxis < fResultHnSparse->GetNdimensions(); iAxis++) {
    idBin                = 1;
    std::string axisType = fMapAxesType->GetXaxis()->GetBinLabel(iAxis + 1);
    if (!hasDataMc) hasDataMc = !axisType.compare("data");
    if (!axisType.compare("proj")) {
      isDataSys = false;
      fProjectionAxes.push_back(iAxis);
    }
    else if (!axisType.compare("sys") || !axisType.compare("data")) {
      if (isDataSys) {
        idTmp = iAxis - iAxisStart;
        idBin = fCfg["ndmspc"]["result"]["data"]["defaults"][idTmp].get<int>();
      }
      else {
        idTmp = iAxis - iAxisStart - fNDimCuts;
        if (histogramEnabled) idTmp -= fCfg["ndmspc"]["result"]["data"]["defaults"].size();
        idBin = axesArray[idTmp]["default"].get<int>() + 1;
      }
    }
    a = (TAxis *)fResultHnSparse->GetAxis(iAxis);
    Printf("Axis: %d [%s][%s] SetRange(%d,%d)", iAxis, axisType.c_str(), a->GetName(), idBin, idBin);
    points[iAxis]          = a->GetNbins();
    fParameterPoint[iAxis] = idBin;
    fResultHnSparse->GetAxis(iAxis)->SetRange(idBin, idBin);
    std::string l = a->GetBinLabel(idBin);
    if (l.empty()) {
      fMapTitle += std::to_string(a->GetBinLowEdge(idBin));
    }
    else {
      fMapTitle += l;
    }
    fMapTitle += " ";
  }
  fMapTitle[fMapTitle.size() - 1] = ']';

  for (auto & p : fParameterPoint) {
    printf("%d ", p);
  }
  printf("\n");

  Printf("fMapTitle='%s'", fMapTitle.c_str());

  auto CanvasMain = new TCanvas("CanvasMain", "CanvasMain", 0, 0, 500, 500);
  CanvasMain->Divide(1, 3);
  /*CanvasMain->HighlightConnect("HighlightMain(TVirtualPad*,TObject*,Int_t,Int_t)");*/
  CanvasMain->Connect("Highlighted(TVirtualPad*,TObject*,Int_t,Int_t)", "NdmSpc::PointDraw", this,
                      "HighlightMain(TVirtualPad*,TObject*,Int_t,Int_t)");

  //   // handle systematics
  //   // Printf("nAxisX=%d nAxisY=%d", nAxisX, nAxisY);
  CanvasMain->cd(1);
  TH1 * hParamMain = new TH1S("hParamMain", "Param Main", fResultHnSparse->GetAxis(0)->GetNbins(), 0,
                              fResultHnSparse->GetAxis(0)->GetNbins());

  for (int i = 0; i < fResultHnSparse->GetAxis(0)->GetNbins(); i++) {
    hParamMain->GetXaxis()->SetBinLabel(i + 1, fResultHnSparse->GetAxis(0)->GetBinLabel(i + 1));
    // hParamMain->SetBinContent(i + 1, 1, 1);
  }

  // for (int i = 0; i < nAxisX; i++) {
  //   hParamMain->GetXaxis()->SetBinLabel(i + 1, fResultHnSparse->GetAxis(i)->GetName());
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
    fResultHnSparse->GetAxis(1)->SetRange();
    fResultHnSparse->GetAxis(2)->SetRange();
    fDataId.clear();
    fDataId.push_back(-1);
    fData.clear();
    fData.push_back("x");
    fMcId.clear();
    fMcId.push_back(-1);

    fMc.clear();
    fMc.push_back("x");
    for (auto & b1 : fCfg["ndmspc"]["data"]["histogram"]["bins"]) {
      std::vector<int> b = b1;
      Printf("b1=[%d(%s),%d(%s),%d(%s),%d(%s)] ", b[0], fResultHnSparse->GetAxis(1)->GetBinLabel(b[0]), b[1],
             fResultHnSparse->GetAxis(2)->GetBinLabel(b[1]), b[2], fResultHnSparse->GetAxis(3)->GetBinLabel(b[2]), b[3],
             fResultHnSparse->GetAxis(4)->GetBinLabel(b[3]));
      std::string v;
      if (b[0] == 1) {
        // mc
        v += fResultHnSparse->GetAxis(2)->GetBinLabel(b[1]);
        v += "|";
        v += std::to_string(b[2]) + "|";
        v += std::to_string(b[3]);
        fMc.push_back(v);
        fMcId.push_back(b[1]);
      }
      else if (b[1] == 1) {
        // data
        v += fResultHnSparse->GetAxis(1)->GetBinLabel(b[0]);
        v += "|";
        v += std::to_string(b[2]) + "|";
        v += std::to_string(b[3]);
        fData.push_back(v);
        fDataId.push_back(b[0]);
      }
      Printf("%s", v.c_str());
    }
    Printf("%ld %ld", fData.size(), fMc.size());
    TH1 * hDataMc = new TH2S("hDataMc", "Data vs. MC", fData.size(), 0, fData.size(), fMc.size(), 0, fMc.size());

    /*TH2 * hDataMc = fResultHnSparse->Projection(2, 1, "O");*/
    /*hDataMc->SetNameTitle("hDataMc", "Data vs. MC");*/
    hDataMc->SetStats(0);
    for (int i = 0; i < fData.size(); i++) {
      hDataMc->GetXaxis()->SetBinLabel(i + 1, fData[i].c_str());
    }
    for (int i = 0; i < fMc.size(); i++) {
      hDataMc->GetYaxis()->SetBinLabel(i + 1, fMc[i].c_str());
    }

    for (int i = 1; i < hDataMc->GetNbinsX() + 1; i++)
      for (int j = 1; j < hDataMc->GetNbinsY() + 1; j++) {
        if (i == 1 && j == 1) continue;
        if (i == 1 || j == 1) hDataMc->SetBinContent(i, j, 1);
      }

    // hDataMc->Reset();
    // hParamMain->SetBinContent(i + 1, j + 1, 1);

    hDataMc->Draw("col");
    hDataMc->SetHighlight();
  }

  DrawProjections();
  CanvasMain->Modified();
  CanvasMain->Update();

  app.Run();
  return 0;
}

void PointDraw::DrawUser()
{
  Printf("DrawUser : Getting '%s' ...", fCurrentContentPath.c_str());
  auto CanvasUser = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasUser");
  if (!CanvasUser) {
    CanvasUser = new TCanvas("CanvasUser", "CanvasUser", 1110, 0, 600, 500);
    // Printf("CanvasUser");
    // CanvasUser->Divide(1, fProjectionAxes.size());
    // CanvasUser->HighlightConnect("HighlightProj(TVirtualPad*,TObject*,Int_t,Int_t)");
  }
  TH1 * hPeak = (TH1 *)fIn->Get(fCurrentContentPath.c_str());
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

void PointDraw::UpdateRanges()
{
  for (int iAxis = 0; iAxis < fResultHnSparse->GetNdimensions(); iAxis++) {
    Printf("Axis: %d [%s] SetRange(%d,%d)", iAxis, fResultHnSparse->GetAxis(iAxis)->GetName(), fParameterPoint[iAxis],
           fParameterPoint[iAxis]);
    fResultHnSparse->GetAxis(iAxis)->SetRange(fParameterPoint[iAxis], fParameterPoint[iAxis]);
  }

  fCurrentContentPath.clear();
  int i = 0;
  for (auto & p : fParameterPoint) {
    if (fCurrentContentPath.empty()) {
      fCurrentContentPath = "content/";
      continue;
    }
    fCurrentContentPath += std::to_string(std::abs(p)) + "/";
  }
  fCurrentContentPath += "hPeak";
}

void PointDraw::DrawProjections()
{

  Printf("DrawProjections %zu", fProjectionAxes.size());
  UpdateRanges();

  if (fParameterPoint.size() == 0) return;

  int    xBin = fParameterPoint[fProjectionAxes[0]];
  int    yBin = fProjectionAxes.size() > 1 ? fParameterPoint[fProjectionAxes[1]] : -1;
  double min  = 1;
  double max  = 0;

  if (!fCfg["ndmspc"]["result"]["parameters"]["draw"][fCurrentParameterName].is_null()) {
    Printf("Apply %s %s", fCurrentParameterName.c_str(),
           fCfg["ndmspc"]["result"]["parameters"]["draw"][fCurrentParameterName].dump().c_str());
    min = fCfg["ndmspc"]["result"]["parameters"]["draw"][fCurrentParameterName]["min"].get<double>();
    max = fCfg["ndmspc"]["result"]["parameters"]["draw"][fCurrentParameterName]["max"].get<double>();
  }

  auto CanvasProjectionMap = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProjectionMap");
  if (!CanvasProjectionMap) {
    CanvasProjectionMap = new TCanvas("CanvasProjectionMap", "CanvasProjectionMap", 505, 0, 600, 500);
    Printf("CanvasProjectionMap xBin=%d yBin=%d %zu", xBin, yBin, fProjectionAxes.size());
    /*CanvasProjectionMap->HighlightConnect("HighlightProjectionPoint(TVirtualPad*,TObject*,Int_t,Int_t)");*/
    CanvasProjectionMap->Connect("Highlighted(TVirtualPad*,TObject*,Int_t,Int_t)", "NdmSpc::PointDraw", this,
                                 "HighlightProjectionPoint(TVirtualPad*,TObject*,Int_t,Int_t)");
  }

  if (fProjectionAxes.size() == 1) {
    CanvasProjectionMap->cd();
    fResultHnSparse->GetAxis(fProjectionAxes[0])->SetRange();
    TH1 * h = fResultHnSparse->Projection(fProjectionAxes[0], "O");
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
  if (fProjectionAxes.size() == 2) {
    CanvasProjectionMap->cd();
    fResultHnSparse->GetAxis(fProjectionAxes[0])->SetRange();
    fResultHnSparse->GetAxis(fProjectionAxes[1])->SetRange();
    fResultHnSparse->Projection(fProjectionAxes[1], fProjectionAxes[0], "O")->Draw("colz");
    TH2 * h = fResultHnSparse->Projection(fProjectionAxes[1], fProjectionAxes[0], "O");
    h->SetHighlight();
    h->Draw("colz");
    CanvasProjectionMap->Modified();
    CanvasProjectionMap->Update();
  }

  auto CanvasProjections = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("CanvasProjections");
  if (!CanvasProjections) {
    CanvasProjections = new TCanvas("CanvasProjections", "CanvasProjections", 505, 700, 600, 500);
    Printf("CanvasProjections xBin=%d yBin=%d %zu", xBin, yBin, fProjectionAxes.size());
    CanvasProjections->Divide(1, fProjectionAxes.size());
    // CanvasProjections->HighlightConnect("HighlightProj(TVirtualPad*,TObject*,Int_t,Int_t)");
  }

  TH1 * px = nullptr;
  TH1 * py = nullptr;
  CanvasProjections->cd(1);
  fResultHnSparse->GetAxis(fProjectionAxes[0])->SetRange();
  fResultHnSparse->GetAxis(fProjectionAxes[1])->SetRange(xBin, xBin);
  px = fResultHnSparse->Projection(fProjectionAxes[0], "O");
  if (px) {
    if (min < max) {
      px->SetMinimum(min);
      px->SetMaximum(max);
    }
    px->Draw();
  }
  if (fProjectionAxes.size() == 2) {
    CanvasProjections->cd(2);
    if (fProjectionAxes.size() > 1) fResultHnSparse->GetAxis(fProjectionAxes[1])->SetRange();
    if (yBin > 0) fResultHnSparse->GetAxis(fProjectionAxes[0])->SetRange(yBin, yBin);
    py = fResultHnSparse->Projection(fProjectionAxes[1], "O");
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

void PointDraw::HighlightMain(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
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

void PointDraw::HighlightParam(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  Printf("HighlightParam %d %d", xBin, yBin);
  TH1 * fParamMapHistogram = (TH1 *)obj;
  if (!fParamMapHistogram) return;

  // double binContent = fParamMapHistogram->GetBinContent(xBin, yBin);
  // if (binContent <= 0) return;
  fParamMapHistogram->SetTitle(fParamMapHistogram->GetXaxis()->GetBinLabel(xBin));
  fParameterPoint[0]    = xBin;
  fCurrentParameterName = fParamMapHistogram->GetXaxis()->GetBinLabel(xBin);
  Printf("%s", fCurrentParameterName.c_str());
  DrawProjections();

  pad->Modified();
  pad->Update();
}

void PointDraw::HighlightData(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  // Printf("HighlightData %d %d", xBin, yBin);
  TH2 * hDataMc = (TH2 *)obj;
  if (!hDataMc) return;

  if (hDataMc->GetBinContent(xBin, yBin) <= 0) {
    hDataMc->SetTitle("Data vs. MC");
    pad->Modified();
    pad->Update();
    return;
  }

  std::string data = hDataMc->GetXaxis()->GetBinLabel(xBin);
  std::string mc   = hDataMc->GetYaxis()->GetBinLabel(yBin);
  hDataMc->SetTitle(TString::Format("%s vs %s", data.c_str(), mc.c_str()).Data());
  Printf("data=%s[%d] mc=%s[%d]", data.c_str(), xBin, mc.c_str(), yBin);
  fParameterPoint[1] = xBin;
  fParameterPoint[2] = yBin;

  if (xBin == 1 && yBin > 1) {
    // mc
    ptrdiff_t pos                = distance(fMc.begin(), find(fMc.begin(), fMc.end(), mc));
    fParameterPoint[2]           = fMcId[int(pos)];
    std::vector<std::string> tok = NdmSpc::Utils::Tokenize(mc, '|');
    fParameterPoint[3]           = atoi(tok[1].c_str());
    fParameterPoint[4]           = atoi(tok[2].c_str());
  }
  else if (yBin == 1 && xBin > 1) {
    // data
    ptrdiff_t pos                = distance(fData.begin(), find(fData.begin(), fData.end(), data));
    fParameterPoint[1]           = fDataId[int(pos)];
    std::vector<std::string> tok = NdmSpc::Utils::Tokenize(data, '|');
    fParameterPoint[3]           = atoi(tok[1].c_str());
    fParameterPoint[4]           = atoi(tok[2].c_str());
  }
  else {
    // TODO: Handle case when mc and data are not one
    return;
  }

  // Printf("fParameterPoint %d %d %d %d", fParameterPoint[1], fParameterPoint[2], fParameterPoint[3],
  // fParameterPoint[4]);

  DrawProjections();
  DrawUser();
  pad->Modified();
  pad->Update();
}

void PointDraw::HighlightProjectionPoint(TVirtualPad * pad, TObject * obj, Int_t xBin, Int_t yBin)
{
  Printf("HighlightProjectionPoint %d %d %d", xBin, yBin, fProjectionAxes[0]);
  if (fProjectionAxes.size() == 1) {
    fParameterPoint[fProjectionAxes[0]] = xBin;
  }
  else if (fProjectionAxes.size() == 2) {
    fParameterPoint[fProjectionAxes[0]] = xBin;
    fParameterPoint[fProjectionAxes[1]] = yBin;
  }
  UpdateRanges();
  DrawUser();
}

} // namespace NdmSpc
