#include <iostream>
#include <string>
#include "TSystem.h"
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
bool wstest(std::string url = "ws://localhost:8080/ws/root.websocket", std::string message = "", int timeout = 60 * 000)
{

  Ndmspc::NWsClient client;
  if (!client.connect(url)) {
    std::cout << "Failed to connect to " << url << std::endl;

    return false;
  }

  std::cout << "Connected to " << url << std::endl;

  if (!message.empty()) {
    if (!client.send(message)) {
      std::cout << "Failed to send message" << std::endl;
      client.disconnect();
      return false;
    }

    std::cout << "Sent: " << message << std::endl;
  }
  TCanvas * c = nullptr;
  int       i = 0;
  while (!gSystem->ProcessEvents()) {
    std::string response = client.receive(timeout); // 2 second timeout
    if (!response.empty()) {
      // Check if this is our echo
      if (!message.empty()) {
        if (response == message || response.find(message) != std::string::npos) {
          std::cout << "✓ Echo received successfully!" << std::endl;
          break; // Stop after receiving the echo
        }
      }
      else {

        i++;
        // std::cout << response << std::endl;

        // remove string ": " and brackets from response
        // response.erase(0, response.find(": ") + 2);

        if (response[0] != '{') {
          Ndmspc::NLogger::Warning("Response '%s' is not json object !!!", response.c_str());
          continue;
          // client.disconnect();
          // return false;
        }

        TObjArray * arr = (TObjArray *)TBufferJSON::ConvertFromJSON(response.c_str());
        if (arr) {

          if (!c) {
            c = new TCanvas("c", "c", 800, 600);
            c->Divide(2, 2);
          }
          TH1 * h = (TH1 *)arr->At(0);
          if (h) {
            c->cd(1);
            h->Draw();
          }
          TH2 * h2 = (TH2 *)arr->At(1);
          if (h2) {
            c->cd(2);
            h2->Draw("colz");
          }
          TH2 * h3 = (TH2 *)arr->At(2);
          if (h3) {
            c->cd(3);
            h3->Draw("colz");
          }
          TH3 * h4 = (TH3 *)arr->At(3);
          if (h4) {
            c->cd(4);
            h4->Draw("colz");
          }

          c->ModifiedUpdate();
        }
      }
    }
  }

  client.disconnect();
  return true;
}
