#include <iostream>
#include <string>
#include <TThread.h>
#include <TVirtualPad.h>
#include <NUtils.h>
#include <TSystem.h>
#include <TROOT.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TCanvas.h>
#include <RQ_OBJECT.h> // Required for signal/slot in classes
#include <TBufferJSON.h>
#include "NHttpRequest.h"
#include "NWsClient.h"
#include "NLogger.h"
///
/// Start server in another terminal:
/// $ ndmspc-cli serve ngnt -m macros/builtin/httpNgnt.C
///
// void DynamicExec()
// {
//   // TThread::Lock();
//   //
//
//   TObject * select = gPad->GetSelected();
//   if (!select) {
//     return;
//   }
//   // if (!select || !select->InheritsFrom(TH1::Class())) return;
//
//   static int lastBin = 0;
//
//   // float uxmin  = gPad->GetUxmin();
//   // float uxmax  = gPad->GetUxmax();
//   // int   pxmin  = gPad->XtoAbsPixel(uxmin);
//   // int   pxmax  = gPad->XtoAbsPixel(uxmax);
//   int px    = gPad->GetEventX();
//   int py    = gPad->GetEventY();
//   int event = gPad->GetEvent();
//
//   NLogDebug("DynamicExec: event=%d px=%d py=%d selected=%s", event, px, py, select->GetName());
//   // if (event != kButton1Down) return;
//
//   TH1 * h1 = (TH1 *)select;
//
//   // gPad->GetCanvas()->FeedbackMode(kTRUE);
//
//   // Step 1: Convert absolute pixel coordinates to the pad's normalized coordinates (0-1 range)
//   Double_t x_pad = gPad->AbsPixeltoX(px);
//   Double_t y_pad = gPad->AbsPixeltoY(py);
//
//   // Step 2: Convert the pad's normalized coordinates to the user's coordinate system (the histogram axes)
//   Double_t x_user = gPad->PadtoX(x_pad);
//   Double_t y_user = gPad->PadtoY(y_pad);
//
//   int      bin          = h1->FindBin(x_user, y_user);
//   bool     isBinChanged = (bin != lastBin);
//   Double_t binContent   = 0;
//   if (isBinChanged) {
//     lastBin    = bin;
//     binContent = h1->GetBinContent(bin);
//     NLogDebug("DynamicExec: px=%d py=%d x_user=%f y_user=%f bin=%zu content=%f", px, py, x_user, y_user, bin,
//               binContent);
//   }
//   // if (!isBinChanged || event != kButton1Down) {
//   //   return;
//   // }
//
//   if (event == kButton1Down) {
//
//     Ndmspc::NHttpRequest request;
//     json                 postData;
//     postData["action"]   = "publish";
//     postData["bin"]      = (int)binContent;
//     std::string response = request.post("http://localhost:8080/", postData.dump());
//     if (response.empty()) {
//       NLogError("DynamicExec: Empty response from server !!!");
//       return;
//     }
//     json respJson = json::parse(response);
//     NLogDebug("DynamicExec: Server response: %s", respJson.dump().c_str());
//   }
//   // TThread::UnLock();
// }
//
bool httpNgntClient(std::string host = "localhost:8080", bool useSSL = false)
{

  Ndmspc::NUtils::EnableMT();
  TH1::AddDirectory(kFALSE);

  std::string httpUrl = (useSSL ? "https://" : "http://") + host;
  std::string wsUrl   = (useSSL ? "wss://" : "ws://") + host + "/ws/root.websocket";

  Ndmspc::NWsClient * client = new Ndmspc::NWsClient();
  if (!client->Connect(wsUrl)) {
    NLogError("Failed to connect to '%s' !!!", wsUrl.c_str());
    return false;
  }

  NLogInfo("Connected to %s", wsUrl.c_str());
  //
  // if (!message.empty()) {
  //   if (!client.Send(message)) {
  //     NLogError("Failed to send message `%s`", message.c_str());
  //     client.Disconnect();
  //     return false;
  //   }
  //
  //   NLogInfo("Sent: %s", message.c_str());
  // }
  TCanvas * c = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("c1");
  if (!c) {

    // c = new TCanvas("c", "c", 800, 600);
    gROOT->MakeDefCanvas();
    c = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("c1");
    c->Divide(2, 2);
    // c->AddExec("DynamicExec", "DynamicExec();");
  }

  // int       i           = 0;
  // int       lastEntries = 0;
  client->SetOnMessageCallback([&c](const std::string & msg) {
    // TThread::Lock();
    // TCanvas * c = (TCanvas *)gROOT->GetListOfCanvases()->FindObject("c1");
    TH1::AddDirectory(kFALSE);
    // NLogDebug("httpNgntClient: [User Callback] Received message: %s", msg.c_str());
    //
    //   std::cout << "Received message: " << msg << std::endl;
    //   // return;
    if (!msg.empty()) {
      // Check if this is our echo
      // remove string ": " and brackets from response
      // response.erase(0, response.find(": ") + 2);

      if (msg[0] != '{') {
        // NLogWarning("Response '%s' is not json object !!!", msg.c_str());
        return;
      }

      json j = json::parse(msg);
      if (j.contains("event") && j["event"] == "heartbeat") {
        // NLogDebug("Heartbeat received: %s", msg.c_str());
        return;
      }

      // NLogDebug("%s", j.dump().c_str());
      if (j.contains("map") && j["map"].contains("obj")) {
        json  mapData = j["map"]["obj"];
        TH1 * h       = (TH1 *)TBufferJSON::ConvertFromJSON(mapData.dump().c_str());

        if (j["map"].contains("handler")) {
          NLogDebug("Handlers: %s", j["map"]["handler"].dump().c_str());
        }

        if (h) {

          h->SetName("hMap");
          h->SetDirectory(0); // Detach from global directory
          TVirtualPad * pad = c->cd(1);

          // Clean up previous histogram to prevent TObjectSpy crash
          TObject * prev = pad->FindObject(h->GetName());
          if (prev) {
            pad->GetListOfPrimitives()->Remove(prev);
            delete prev;
          }

          h->SetStats(kFALSE);
          h->Draw("colz text");
          pad->Modified();
          pad->Update();
          // pad->ModifiedUpdate();
          // c->cd();
          // c->ModifiedUpdate();
        }
      }

      else if (j.contains("content")) {
        TList * listObj = (TList *)TBufferJSON::ConvertFromJSON(j["content"].dump().c_str());
        if (!listObj) return;

        TH1 * h = (TH1 *)listObj->At(0);
        if (h) {
          h->SetName("hContent");
          h->SetDirectory(0); // Very important!
          listObj->Remove(h); // Hand over ownership to us

          TThread::Lock();
          TVirtualPad * pad = c->cd(2);

          // Find old object by name
          TObject * old = pad->FindObject(h->GetName());
          if (old) {
            // Un-draw it immediately so the GUI stops looking at it
            pad->GetListOfPrimitives()->Remove(old);
            delete old;
          }

          h->Draw("colz");
          pad->Modified();
          pad->Update();
          // TThread::UnLock();
        }

        listObj->SetOwner(kTRUE);
        delete listObj;
      }
    }
    // c->Modified();
    // c->Update();
    c->cd();
    // TThread::UnLock();
  });
  while (!gSystem->ProcessEvents()) {
    gSystem->Sleep(100);
  }

  if (client->IsConnected()) client->Disconnect();
  return true;
}
