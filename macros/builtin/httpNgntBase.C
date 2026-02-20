#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>

void httpNgntBase()
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
    else if (method.find("PATCH") != std::string::npos) {
      httpOut["result"] = "success";
    }

    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for test action";
    }
  };

  // Store lambdas (must be non-capturing to convert to function pointer)
  handlers["debug"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNGnHttpServer;

    NLogInfo("/debug method=%s in=%s", method.c_str(), httpIn.dump().c_str());
    httpOut["httpIn"] = httpIn;
    httpOut["method"] = method;
    if (method.find("GET") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("POST") != std::string::npos) {
      // wsOut["health"] = "ok";
      // server->WebSocketBroadcast(wsOut);
      httpOut["result"] = "success";
      // httpOut["result"] = "skipped";
    }
    else if (method.find("DELETE") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else if (method.find("PATCH") != std::string::npos) {
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for test action";
    }
  };


  handlers["reset"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                         std::map<std::string, TObject *> & inputs) {
    auto server = Ndmspc::gNGnHttpServer;

    if (method.find("POST") != std::string::npos) {
      NLogInfo("Resetting API history and clearing all objects");
      
      try {
        // First, manually remove all stored objects
        std::vector<std::string> keysToRemove;
        for (const auto & pair : inputs) {
          keysToRemove.push_back(pair.first);
        }
        for (const auto & key : keysToRemove) {
          NLogInfo("Removing input object: %s", key.c_str());
          server->RemoveInputObject(key);
        }
        NLogInfo("All input objects removed");
        
        // Clear the history - this should now be safe since we've cleaned up objects
        server->ClearHistory();
        NLogInfo("History cleared successfully");
        
        httpOut = json::object();
        httpOut["result"] = "success";
        httpOut["message"] = "API history and objects reset successfully";
      } catch (const std::exception & e) {
        NLogError("Error during reset: %s", e.what());
        httpOut = json::object();
        httpOut["result"] = "failure";
        httpOut["error"] = std::string("Error during reset: ") + e.what();
      }
    }
    else {
      httpOut = json::object();
      httpOut["error"] = "Unsupported HTTP method for reset action";
      httpOut["result"] = "failure";
    }
  };
}
