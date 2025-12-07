#include <string>
#include <vector>
#include <THnSparse.h>
#include <TCanvas.h>
#include <TH1.h>
#include <TBufferJSON.h>
#include "NLogger.h"
#include "NHnSparseTree.h"
#include "NHnSparseTreePoint.h"
#include <nlohmann/json.hpp>
#include "TVirtualPad.h"
using json = nlohmann::json;

#include "NWsClient.h"

Ndmspc::NWsClient *     wsClient = nullptr;
Int_t                   binx_old = -1;
Int_t                   biny_old = -1;
Ndmspc::NHnSparseTree * ngnt     = nullptr;

void Interactive()
{
  if (!gPad) {
    Error("hover", "gPad is null, you are not supposed to run this macro");
    return;
  }

  int event = gPad->GetEvent();
  if (!(event == kButton1Down || event == kMouseMotion)) {
    // if (!(event == kButton1Down)) {
    return; // only handle mouse motion events
  }
  int       eventX = gPad->GetEventX();
  int       eventY = gPad->GetEventY();
  Float_t   upx    = gPad->AbsPixeltoX(eventX);
  Float_t   x      = gPad->PadtoX(upx);
  Float_t   upy    = gPad->AbsPixeltoY(eventY);
  Float_t   y      = gPad->PadtoY(upy);
  TObject * select = gPad->GetSelected();
  gPad->GetCanvas()->FeedbackMode(kTRUE);

  Int_t binx = -1;
  Int_t biny = -1;

  if (select && select->InheritsFrom(TH2::Class())) {
    TH2 * h2 = dynamic_cast<TH2 *>(select);
    binx     = h2->GetXaxis()->FindBin(x);
    biny     = h2->GetYaxis()->FindBin(y);
  }
  else if (select && select->InheritsFrom(TH1::Class())) {
    TH1 * h1 = dynamic_cast<TH1 *>(select);
    binx     = h1->GetXaxis()->FindBin(x);
  }
  else {
    NLogTrace("Interactive: no TH1 or TH2 selected, skipping");
    return;
  }

  if (event == kMouseMotion && binx == binx_old && biny == biny_old) {
    // No change in bin selection, skip further processing
    return;
  }

  binx_old = binx;
  biny_old = biny;

  // Int_t   nDim     = ngnt->GetNdimensions();
  // Int_t * binCoord = new Int_t[nDim];
  // for (Int_t i = 0; i < nDim; ++i) {
  //   binCoord[i] = 1; // Initialize all dimensions to 0
  // }

  std::vector<int> point = ngnt->GetPoint()->GetPointContent();
  point[5]               = binx; // Set the x bin coordinate
  point[8]               = biny; // Set the y bin coordinate
  ngnt->GetPoint()->SetPointContent(point);
  Long64_t entry = ngnt->GetPoint()->GetEntryNumber(); // Update the entry number
  NLogDebug("Interactive: entry=%lld, binx=%d, biny=%d", entry, binx, biny);
  ngnt->GetEntry(entry); // Load the entry in the HnSparseTree)
  //
  NLogDebug("Interactive: selected object name='%s', title='%s'", select->GetName(), select->GetTitle());
  ((TNamed *)select)->SetTitle(ngnt->GetPoint()->GetTitle("Map").c_str());
  gPad->GetCanvas()->ModifiedUpdate();

  TObjArray * arr = new TObjArray();
  arr->SetOwner(kTRUE);
  THnSparse * unlikep    = (THnSparse *)ngnt->GetBranchObject("unlikepm");
  TH1 *       projection = nullptr;
  if (unlikep) {
    projection = unlikep->Projection(0);
    projection->SetTitle(ngnt->GetPoint()->GetTitle("unlikepm").c_str());
    arr->Add(projection);
  }
  else {
    NLogError("Interactive: unlikepm branch not found in HnSparseTree");
  }

  THnSparse * likepp           = (THnSparse *)ngnt->GetBranchObject("likepp");
  TH1 *       likeppProjection = nullptr;
  if (likepp) {
    likeppProjection = likepp->Projection(0);
    likeppProjection->SetTitle(ngnt->GetPoint()->GetTitle("likepm").c_str());
    arr->Add(likeppProjection);
  }
  else {
    NLogError("Interactive: likepm branch not found in HnSparseTree");
  }

  THnSparse * likemm           = (THnSparse *)ngnt->GetBranchObject("likemm");
  TH1 *       likemmProjection = nullptr;
  if (likemm) {
    likemmProjection = likemm->Projection(0);
    likemmProjection->SetTitle(ngnt->GetPoint()->GetTitle("likemm").c_str());
    arr->Add(likemmProjection);
  }
  else {
    NLogError("Interactive: likemm branch not found in HnSparseTree");
  }

  THnSparse * mixingpm           = (THnSparse *)ngnt->GetBranchObject("mixingpm");
  TH1 *       mixingpmProjection = nullptr;
  if (mixingpm) {
    mixingpmProjection = mixingpm->Projection(0);
    mixingpmProjection->SetTitle(ngnt->GetPoint()->GetTitle("mixingpm").c_str());
    arr->Add(mixingpmProjection);
  }
  else {
    NLogError("Interactive: mixingpm branch not found in HnSparseTree");
  }

  // Print bin coordinates
  // NLogInfo("Interactive: name='%s' bin: [%d,%d]", select->GetName(), binx, biny);
  // json data;
  // data["event"] = event == kButton1Down ? "click" : event == kMouseMotion ? "hover" : "unknown";
  // data["name"]  = select->GetName();
  // data["binx"]  = binx;
  // data["biny"]  = biny;

  std::string data;
  if (arr->GetEntries() > 0) {
    data = TBufferJSON::ConvertToJSON(arr);
  }

  if (wsClient == nullptr) {
    NLogInfo("Interactive: creating WebSocket client");
    wsClient = new Ndmspc::NWsClient();
    if (!wsClient->Connect("ws://localhost:8080/ws/root.websocket")) {
      NLogError("Failed to connect to WebSocket server");
      delete wsClient;
      wsClient = nullptr;
      return;
    }

    wsClient->SetOnMessageCallback([](const std::string & msg) {
      NLogTrace("Interactive: [User Callback] Received message: %s", msg.c_str());
      // Process message here.
    });
  }
  // NLogDebug("Interactive: data=%s", data.dump().c_str());
  // NLogDebug("Interactive: data=%s", data.c_str());
  if (wsClient->IsConnected()) {
    static std::chrono::steady_clock::time_point lastSendTime;
    static constexpr auto                        debounceInterval = std::chrono::milliseconds(10);
    auto                                         now              = std::chrono::steady_clock::now();
    if (now - lastSendTime < debounceInterval) {
      return;
    }
    lastSendTime = now;
    NLogTrace("NHnSparseTreeDraw: sending data to WebSocket server");
    bool rc = wsClient->Send(data.c_str());
  }
}

void NHnSparseTreeDraw(std::string filename = "$HOME/.ndmspc/dev/ngnt.root", std::string enabledBranches = "")
{
  TH1::AddDirectory(kFALSE);

  ngnt = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (ngnt == nullptr) {
    return;
  }

  // ngnt->Print("P");
  // NLogInfo("Entries=%d", ngnt->GetEntries());
  TCanvas * c = new TCanvas("c", "HNST Draw", 800, 600);
  auto *    h = ngnt->Projection(2, 1);
  h->SetMinimum(0);
  h->SetStats(0);
  h->Draw("colz");
  gPad->AddExec("Interactive", "Interactive()");
}
