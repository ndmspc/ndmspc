#include <iostream>
#include <string>
#include "TMath.h"
#include <NUtils.h>
#include <TSystem.h>
#include <TROOT.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TCanvas.h>
#include <TBufferJSON.h>
#include "NWsClient.h"
#include "NLogger.h"
///
/// Start server in another terminal:
/// $ ndmspc-cli serve stress
///
bool wstest(std::string url = "ws://localhost:8080/ws/root.websocket", std::string message = "")
{

  // enable ROOT multithreading
  ROOT::EnableThreadSafety();
  // Enable IMT with default number of threads (usually number of CPU cores)
  ROOT::EnableImplicitMT();

  // Check if IMT is enabled
  if (ROOT::IsImplicitMTEnabled()) {
    std::cout << "IMT is enabled with " << ROOT::GetThreadPoolSize() << " threads" << std::endl;
  }

  // TH1::AddDirectory(kFALSE);
  //

  Ndmspc::NWsClient client;
  if (!client.Connect(url)) {
    NLogError("Failed to connect to '%s' !!!", url.c_str());
    return false;
  }

  NLogInfo("Connected to %s", url.c_str());

  if (!message.empty()) {
    if (!client.Send(message)) {
      NLogError("Failed to send message `%s`", message.c_str());
      client.Disconnect();
      return false;
    }

    NLogInfo("Sent: %s", message.c_str());
  }
  TCanvas * c           = new TCanvas("c", "c", 800, 600);
  int       i           = 0;
  int       lastEntries = 0;
  client.SetOnMessageCallback([&c, &lastEntries](const std::string & msg) {
    // NLogDebug("Interactive: [User Callback] Received message: %s", msg.c_str());

    std::cout << "Received message: " << msg << std::endl;
    // return;
    if (!msg.empty()) {
      // Check if this is our echo
      // remove string ": " and brackets from response
      // response.erase(0, response.find(": ") + 2);

      if (msg[0] != '{') {
        NLogWarning("Response '%s' is not json object !!!", msg.c_str());
        return;
      }

      json j = json::parse(msg);
      if (j.contains("event") && j["event"] == "heartbeat") {
        NLogDebug("Heartbeat received: %s", msg.c_str());
        return;
      }

      // NLogDebug("%s", j.dump(2).c_str());

      TObjArray * arr = (TObjArray *)TBufferJSON::ConvertFromJSON(msg.c_str());
      if (arr == nullptr) {
        NLogError("Failed to convert JSON to TObjArray: %s", msg.c_str());
        return;
      }
      // arr->Print();
      // NLogDebug("Received message: %p", (void *)arr);
      if (arr) {

        if (c == nullptr || arr->GetEntries() != lastEntries) {
          lastEntries = arr->GetEntries();
          Int_t cx    = TMath::Sqrt(lastEntries);
          Int_t cy    = TMath::Ceil(lastEntries / (double)cx);
          c->Clear();
          c->Divide(cx, cy);
        }
        for (int i = 0; i < arr->GetEntries(); i++) {
          TObject * obj = arr->At(i);
          if (obj) {
            c->cd(i + 1);
            if (obj->InheritsFrom("TH1")) {
              TH1 * h = (TH1 *)obj;
              h->SetDirectory(nullptr); // avoid memory issues
              h->Draw();
            }
            else if (obj->InheritsFrom("TH2")) {
              TH2 * h2 = (TH2 *)obj;
              h2->SetDirectory(nullptr); // avoid memory issues
              h2->Draw("colz");
            }
            else if (obj->InheritsFrom("TH3")) {
              TH3 * h3 = (TH3 *)obj;
              h3->SetDirectory(nullptr); // avoid memory issues
              h3->Draw("colz");
            }
            // else {
            //   NLogWarning("Object %d is not a histogram: %s", i, obj->ClassName());
            // }
          }
          else {
            return;
          }
        }

        c->ModifiedUpdate();
      }
    }
  });

  while (!gSystem->ProcessEvents()) {
    gSystem->Sleep(100);
  }

  if (client.IsConnected()) client.Disconnect();
  return true;
}
