#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>

void httpNgntHealth()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // Store lambdas (must be non-capturing to convert to function pointer)
  handlers["health"] = [](std::string method, json & /*httpIn*/, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNGnHttpServer;

    if (method.find("GET") != std::string::npos) {
      server->Print();
      httpOut["result"] = "success";
    }
    else if (method.find("POST") != std::string::npos) {
      // wsOut["health"] = "ok";
      // server->WebSocketBroadcast(wsOut);
      httpOut["result"] = "skipped";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for test action";
    }
  };
}
