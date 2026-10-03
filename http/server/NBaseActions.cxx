// The server's own base actions (health, state), as framework code.
//
// NBaseActions.h says what they are and why they are not a macro; this file is their
// implementation. The two handlers are lifted from the toolBase.C macro they replace, and their
// lambdas stay non-capturing so they convert to the NHttpFuncPtr the handler map holds.

#include "NBaseActions.h"

#include <map>
#include <string>

#include "ndmspc/core/NLogger.h"
#include "ndmspc/http/NHttpServer.h"
#include "ndmspc/http/NInstanceTree.h"

namespace Ndmspc {

bool RegisterBaseActions()
{
  if (gNdmspcHttpHandlers == nullptr) return false;

  auto & handlers = *(gNdmspcHttpHandlers);

  // MCP tool metadata (see toolNgnt.C for the convention)
  Ndmspc::RegisterMcpTool("health", {
      .description = "Server health and workspace snapshot (GET prints the server, POST/PATCH return the workspace).",
      .methods     = {"GET", "POST", "PATCH", "DELETE"},
  });
  Ndmspc::RegisterMcpTool("state", {
      .description = "Inspect or reset server state: GET returns the workspace inspector schema, PATCH updates "
                     "the heartbeat, DELETE resets the server.",
      .methods     = {"GET", "PATCH", "DELETE"},
  });
  Ndmspc::RegisterMcpTool("group", {
      .description = "The tool group the room is looking at: GET reports it, PATCH sets it for every client.",
      .methods     = {"GET", "PATCH", "POST"},
  });

  handlers["health"] = [](std::string method, json & /*httpIn*/, json & httpOut, json & wsOut,
                          std::map<std::string, TObject *> &) {
    auto server = Ndmspc::gNHttpServer;

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

  handlers["state"] = [](std::string method, json & httpIn, json & httpOut, json & /*wsOut*/,
                         std::map<std::string, TObject *> & /*inputs*/) {
    auto server = Ndmspc::gNHttpServer;

    if (method.find("GET") != std::string::npos) {
      // Return current server workspaces and state, and inspector entries
      try {
        auto schema = server->GetInspectorSchema();

        httpOut["result"] = "success";
        httpOut["payload"]["title"] = schema["title"];
        httpOut["payload"]["inspector"] = schema["inspector"];
        httpOut["payload"]["metadata"] = schema["metadata"];
        httpOut["payload"]["state"]["heartbeat"] = server->GetHeartbeatMs();
        httpOut["payload"]["combinations"] = Ndmspc::NInstanceTree(server->GetCombinations()).ToTree();
        NLogInfo("State GET inspector: %s", schema["inspector"].dump().c_str());
      }
      catch (const std::exception & e) {
        NLogError("Error during state GET: %s", e.what());
        httpOut = json::object();
        httpOut["result"] = "failure";
        httpOut["error"] = std::string("Error during state GET: ") + e.what();
      }
    }
    else if (method.find("PATCH") != std::string::npos) {
      // Allow runtime changes to server state, e.g. heartbeat timeout
      try {
        if (httpIn.contains("heartbeat") && httpIn["heartbeat"].is_number()) {
          int hb = httpIn["heartbeat"].get<int>();
          server->SetHeartbeatMs(hb);
          httpOut["result"] = "success";
          httpOut["heartbeat"] = hb;
        } else {
          httpOut["result"] = "failure";
          httpOut["error"] = "Missing or invalid 'heartbeat' field";
        }
      } catch (const std::exception & e) {
        NLogError("Error during state PATCH: %s", e.what());
        httpOut["result"] = "failure";
        httpOut["error"] = std::string("Error during state PATCH: ") + e.what();
      }
    }
    else if (method.find("DELETE") != std::string::npos) {
      NLogInfo("Resetting API history and clearing all objects");
      try {
        server->ResetServer();
        httpOut["result"] = "success";
        httpOut["message"] = "API history and objects reset successfully";
      } catch (const std::exception & e) {
        NLogError("Error during state reset: %s", e.what());
        httpOut["result"] = "failure";
        httpOut["error"] = std::string("Error during state reset: ") + e.what();
      }
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for state action";
      httpOut["result"] = "failure";
    }
  };

  handlers["group"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                         std::map<std::string, TObject *> & /*inputs*/) {
    auto server = Ndmspc::gNHttpServer;

    if (method.find("GET") != std::string::npos) {
      httpOut["result"]             = "success";
      httpOut["payload"]["group"]   = server->GetGroup();
    }
    else if (method.find("PATCH") != std::string::npos || method.find("POST") != std::string::npos) {
      // One room, one tool: everyone looking at it sees the same pipeline, so a switch is told to every
      // client (the payload rides the broadcast) rather than kept to the one that made it. It is also
      // what a client that joins later is handed (see NHttpServer::SessionState).
      const std::string group = httpIn.value("group", std::string());
      server->SetGroup(group);
      wsOut["payload"]["group"]   = group;
      httpOut["result"]           = "success";
      httpOut["payload"]["group"] = group;

      // A session belongs to a group, so the room's session follows the group it is looking at: that
      // group's own session becomes the one everyone is on. Otherwise a group with sessions would have
      // no current one - and nothing to show as its name - until somebody picked one.
      const std::string session = server->GroupSession(group);
      if (!session.empty()) {
        server->ActivateSession(session);
        wsOut["payload"]["session"]  = session;
        wsOut["payload"]["sessions"] = server->SessionList()["sessions"];
      }
    }
    else {
      httpOut["result"] = "failure";
      httpOut["error"]  = "Unsupported HTTP method for group action";
    }
  };

  Ndmspc::RegisterMcpTool("session", {
      .description = "The room's sessions: GET lists them and the one the room is on, PATCH makes one "
                     "the room's (and renames it when given a `name`), or starts a fresh one for a "
                     "group with `new` - whose first step then fills it.",
      .methods     = {"GET", "PATCH", "POST"},
  });

  handlers["session"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                           std::map<std::string, TObject *> & /*inputs*/) {
    auto server = Ndmspc::gNHttpServer;

    if (method.find("GET") != std::string::npos) {
      httpOut["result"]  = "success";
      httpOut["payload"] = server->SessionList();
      return;
    }
    if (method.find("PATCH") != std::string::npos || method.find("POST") != std::string::npos) {
      // A rename rides along when one is given: what the session is called, not what it is (see
      // NHttpServer::RenameSession).
      const std::string name = httpIn.value("name", std::string());
      // Or a fresh session for a group: the room starts it, names it, and it becomes the one everybody
      // is on - its first step then fills it rather than starting a session of its own (StartSession).
      std::string session = httpIn.value("session", std::string());
      if (session.empty() && httpIn.value("new", false)) {
        session = server->StartSession(httpIn.value("group", std::string()), name);
      }
      if (session.empty() || !server->ActivateSession(session)) {
        httpOut["result"] = "failure";
        httpOut["error"]  = "not a session of this room: '" + session + "'";
        return;
      }
      if (!name.empty() && !server->RenameSession(session, name)) {
        httpOut["result"] = "failure";
        httpOut["error"]  = "not a session of this room: '" + session + "'";
        return;
      }
      // Told to every client, so they all follow the same session (the tree's active path moved) and
      // their picker shows the list as it now stands.
      const json list                  = server->SessionList();
      wsOut["payload"]["session"]      = session;
      wsOut["payload"]["sessions"]     = list["sessions"];
      wsOut["payload"]["combinations"] = Ndmspc::NInstanceTree(server->GetCombinations()).ToTree();
      httpOut["result"]                = "success";
      httpOut["payload"]               = list;
      return;
    }
    httpOut["result"] = "failure";
    httpOut["error"]  = "Unsupported HTTP method for session action";
  };

  return true;
}

} // namespace Ndmspc
