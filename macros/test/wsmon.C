#include <string>
#include <TROOT.h>
#include <TSystem.h>
#include <NUtils.h>
#include "NWsClient.h"
#include "NLogger.h"
///
/// Start server in another terminal:
/// $ ndmspc-cli serve stress
///
bool wsmon(std::string url = "ws://localhost:8080/ws/root.websocket")
{

  Ndmspc::NWsClient client;
  if (!client.Connect(url)) {
    NLogError("Failed to connect to '%s' !!!", url.c_str());
    return false;
  }

  NLogInfo("Connected to %s", url.c_str());

  client.SetOnMessageCallback([](const std::string & msg) {
    NLogDebug("Monitor: [User Callback] Received message: %s", msg.c_str());
    if (!msg.empty()) {

      if (msg[0] != '{') {
        NLogWarning("Response '%s' is not json object !!!", msg.c_str());
        return;
      }

      json j = json::parse(msg);
      if (j.contains("event") && j["event"] == "heartbeat") {
        NLogDebug("Heartbeat received: %s", msg.c_str());
        return;
      }
    }
  });

  while (!gSystem->ProcessEvents()) {
    gSystem->Sleep(100);
  }

  if (client.IsConnected()) client.Disconnect();
  return true;
}
