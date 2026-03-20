#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>
#include <NParameters.h>

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
      // wsOut["workspace"] = server->GetWorkspace();
      wsOut["payload"]["workspace"] = server->GetWorkspace();
      // wsOut["health"] = "ok";
      // server->WebSocketBroadcast(wsOut);
      httpOut["result"] = "success";
    }
    else if (method.find("PATCH") != std::string::npos) {
      wsOut["payload"]["workspace"] = server->GetWorkspace();
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


  handlers["state"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> & inputs) {
    auto server = Ndmspc::gNGnHttpServer;

    if (method.find("GET") != std::string::npos) {
      // Return current server workspaces and state, and inspector entries
      try {
        auto schema = server->GetInspectorSchema();

        httpOut = json::object();
        httpOut["result"] = "success";
        httpOut["payload"]["title"] = schema["title"];
        httpOut["payload"]["inspector"] = schema["inspector"];
        httpOut["payload"]["metadata"] = schema["metadata"];
        NLogInfo("State GET inspector: %s", schema["inspector"].dump().c_str());
        // auto workspaces = server->GetOrderedWorkspace();
        // auto inspectorEntries = server->GetInspectorEntries();
        // auto state = server->GetState();
        // httpOut = json::object();
        // httpOut["result"] = "success";
        // httpOut["workspaces"] = workspaces;
        // httpOut["state"] = state;
        // wsOut["payload"]["workspaces"] = workspaces;
        // wsOut["payload"]["state"] = state;
      } catch (const std::exception & e) {
        NLogError("Error during state GET: %s", e.what());
        httpOut = json::object();
        httpOut["result"] = "failure";
        httpOut["error"] = std::string("Error during state GET: ") + e.what();
      }
    }
    else if (method.find("DELETE") != std::string::npos) {
      NLogInfo("Resetting API history and clearing all objects");
      try {
        server->ResetServer();
        httpOut = json::object();
        httpOut["result"] = "success";
        httpOut["message"] = "API history and objects reset successfully";
      } catch (const std::exception & e) {
        NLogError("Error during state reset: %s", e.what());
        httpOut = json::object();
        httpOut["result"] = "failure";
        httpOut["error"] = std::string("Error during state reset: ") + e.what();
      }
    }
    else {
      httpOut = json::object();
      httpOut["error"] = "Unsupported HTTP method for state action";
      httpOut["result"] = "failure";
    }
  };
}
