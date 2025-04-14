#include <cstddef>
#include <iostream>
#include <string>
#include "TSystem.h"
#include <TH1.h>
#include <TCanvas.h>
#include <TBufferJSON.h>
#include "NWsClient.h"

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
        std::cout << "Event: " << i << std::endl;
        TH1 * h = (TH1 *)TBufferJSON::ConvertFromJSON(response.c_str());
        if (h) {
          if (!c) c = new TCanvas("c", "c", 800, 600);
          h->Draw();
          c->Update();
          c->Modified();
        }
      }
    }
  }
  std::cout << "AAAA" << std::endl;

  client.disconnect();
  return true;
}
