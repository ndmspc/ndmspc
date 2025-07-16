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
using json = nlohmann::json;

#include "NWsClient.h"

Ndmspc::NWsClient *     wsClient = nullptr;
Int_t                   binx_old = -1;
Int_t                   biny_old = -1;
Ndmspc::NHnSparseTree * hnst     = nullptr;

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
    Ndmspc::NLogger::Trace("Interactive: no TH1 or TH2 selected, skipping");
    return;
  }

  if (event == kMouseMotion && binx == binx_old && biny == biny_old) {
    // No change in bin selection, skip further processing
    return;
  }

  binx_old = binx;
  biny_old = biny;

  // Int_t   nDim     = hnst->GetNdimensions();
  // Int_t * binCoord = new Int_t[nDim];
  // for (Int_t i = 0; i < nDim; ++i) {
  //   binCoord[i] = 1; // Initialize all dimensions to 0
  // }

  std::vector<int> point = hnst->GetPoint()->GetPointContent();
  point[5]               = binx; // Set the x bin coordinate
  point[8]               = biny; // Set the y bin coordinate
  hnst->GetPoint()->SetPointContent(point);
  Long64_t entry = hnst->GetPoint()->GetEntryNumber(); // Update the entry number
  Ndmspc::NLogger::Debug("Interactive: entry=%lld, binx=%d, biny=%d", entry, binx, biny);
  hnst->GetEntry(entry); // Load the entry in the HnSparseTree)

  TObjArray * arr = new TObjArray();
  arr->SetOwner(kTRUE);
  THnSparse * unlikep    = (THnSparse *)hnst->GetBranchObject("unlikepm");
  TH1 *       projection = nullptr;
  if (unlikep) {
    projection = unlikep->Projection(0);
    projection->SetTitle(hnst->GetPoint()->GetTitle("unlikepm").c_str());
    arr->Add(projection);
  }
  else {
    Ndmspc::NLogger::Error("Interactive: unlikepm branch not found in HnSparseTree");
  }

  THnSparse * likepp           = (THnSparse *)hnst->GetBranchObject("likepp");
  TH1 *       likeppProjection = nullptr;
  if (likepp) {
    likeppProjection = likepp->Projection(0);
    likeppProjection->SetTitle(hnst->GetPoint()->GetTitle("likepm").c_str());
    arr->Add(likeppProjection);
  }
  else {
    Ndmspc::NLogger::Error("Interactive: likepm branch not found in HnSparseTree");
  }

  THnSparse * likemm           = (THnSparse *)hnst->GetBranchObject("likemm");
  TH1 *       likemmProjection = nullptr;
  if (likemm) {
    likemmProjection = likemm->Projection(0);
    likemmProjection->SetTitle(hnst->GetPoint()->GetTitle("likemm").c_str());
    arr->Add(likemmProjection);
  }
  else {
    Ndmspc::NLogger::Error("Interactive: likemm branch not found in HnSparseTree");
  }

  THnSparse * mixingpm           = (THnSparse *)hnst->GetBranchObject("mixingpm");
  TH1 *       mixingpmProjection = nullptr;
  if (mixingpm) {
    mixingpmProjection = mixingpm->Projection(0);
    mixingpmProjection->SetTitle(hnst->GetPoint()->GetTitle("mixingpm").c_str());
    arr->Add(mixingpmProjection);
  }
  else {
    Ndmspc::NLogger::Error("Interactive: mixingpm branch not found in HnSparseTree");
  }

  // Print bin coordinates
  // Ndmspc::NLogger::Info("Interactive: name='%s' bin: [%d,%d]", select->GetName(), binx, biny);
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
    Ndmspc::NLogger::Info("Interactive: creating WebSocket client");
    wsClient = new Ndmspc::NWsClient();
    if (!wsClient->Connect("ws://localhost:8080/ws/root.websocket")) {
      Ndmspc::NLogger::Error("Failed to connect to WebSocket server");
      delete wsClient;
      wsClient = nullptr;
      return;
    }

    wsClient->SetOnMessageCallback([](const std::string & msg) {
      Ndmspc::NLogger::Trace("Interactive: [User Callback] Received message: %s", msg.c_str());
      // Process message here.
    });
  }
  // Ndmspc::NLogger::Debug("Interactive: data=%s", data.dump().c_str());
  // Ndmspc::NLogger::Debug("Interactive: data=%s", data.c_str());
  if (wsClient->IsConnected()) {
    Ndmspc::NLogger::Trace("NHnSparseTreeDraw: sending data to WebSocket server");
    // bool rc = wsClient->Send(data.dump());
    static std::chrono::steady_clock::time_point lastSentTime = std::chrono::steady_clock::now();
    static std::string                           cachedMsg;
    static const std::chrono::milliseconds       sendTimeout(250); // 500ms throttle

    auto now  = std::chrono::steady_clock::now();
    cachedMsg = data; // Always cache the most recent message

    if (now - lastSentTime >= sendTimeout) {
      bool rc      = wsClient->Send(cachedMsg);
      lastSentTime = now;
      cachedMsg.clear();
    }
  }
}

void NHnSparseTreeDraw(std::string filename = "$HOME/.ndmspc/dev/hnst.root", std::string enabledBranches = "")
{
  TH1::AddDirectory(kFALSE);

  hnst = Ndmspc::NHnSparseTree::Open(filename.c_str(), enabledBranches);
  if (hnst == nullptr) {
    return;
  }

  // hnst->Print("P");
  // Ndmspc::NLogger::Info("Entries=%d", hnst->GetEntries());
  TCanvas * c = new TCanvas("c", "HNST Draw", 800, 600);
  auto *    h = hnst->Projection(2, 1);
  h->SetMinimum(0);
  h->SetStats(0);
  h->Draw("colz");
  gPad->AddExec("Interactive", "Interactive()");
}
