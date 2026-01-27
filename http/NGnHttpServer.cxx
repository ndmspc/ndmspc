#include <NGnTree.h>
#include <TTimer.h>
#include "NLogger.h"
#include "NGnHttpServer.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnHttpServer);
/// \endcond

namespace Ndmspc {
NGnHttpServer::NGnHttpServer(const char * engine, bool ws, int heartbeat_ms) : NHttpServer(engine, ws, heartbeat_ms)
{
  if (ws) {
    fNWsHandler = new NGnWsHandler("ws", "ws");
    Register("/", fNWsHandler);
    TTimer * heartbeat = new TTimer(fNWsHandler, heartbeat_ms);
    heartbeat->Start();
  }
}

void NGnHttpServer::Print(Option_t * option) const
{
  if (fNGnTree) {
    fNGnTree->Print(option);
  }
  else {
    NLogWarning("NGnTree is not set.");
  }
}

void NGnHttpServer::ProcessRequest(std::shared_ptr<THttpCallArg> arg)
{

  // NLogInfo("NGnHttpServer::ProcessRequest");

  TString method = arg->GetMethod();
  if (arg->GetRequestHeader("Content-Type").CompareTo("application/json")) {
    NLogWarning("Unsupported Content-Type: %s", arg->GetRequestHeader("Content-Type").Data());
    return;
  }
  json in;
  try {
    in = json::parse((const char *)arg->GetPostData());
  }
  catch (json::parse_error & e) {
    NLogError("JSON parse error: %s", e.what());
    arg->SetContentType("application/json");
    arg->SetContent("{\"error\": \"Invalid JSON format\"}");
    return;
  }
  NLogInfo("Received %s request with content: %s", method.Data(), in.dump().c_str());

  json out;
  // Process GET/POST/UPDATE request
  if (method == "GET") {
    NLogInfo("Processing GET request");
    if (in.contains("action")) {
      if (in["action"] == "info") {
        if (fNGnTree) {
          out["ngnt"]["file"]     = fNGnTree->GetStorageTree()->GetFileName();
          out["ngnt"]["entries"]  = fNGnTree->GetStorageTree()->GetTree()->GetEntries();
          out["ngnt"]["branches"] = fNGnTree->GetStorageTree()->GetBrancheNames();
        }
      }
      else {
        NLogWarning("Unknown action: %s", in["action"].get<std::string>().c_str());
        out["error"] = "Unknown action";
      }
    }
  }
  else if (method == "POST") {
    NLogInfo("Processing POST request");
    if (in.contains("action")) {
      if (in["action"] == "open") {
        if (in.contains("file")) {
          std::string file = in["file"].get<std::string>().c_str();
          if (file.empty()) {
            out["error"] = "Empty 'file' parameter for open action";
          }
          else {

            if (fNGnTree) {
              // fNGnTree->Close();
              delete fNGnTree;
              fNGnTree = nullptr;
            }

            fNGnTree = Ndmspc::NGnTree::Open(file);
            if (fNGnTree && !fNGnTree->IsZombie()) {
              NLogInfo("Opened NGnTree from file: %s", file.c_str());
              fNGnTree->Print();
            }
            else {
              NLogError("Failed to open NGnTree from file: %s", file.c_str());
            }
            out["result"] = fNGnTree ? "success" : "failure";
          }
        }
        else {
          out["error"] = "Missing 'file' parameter for open action";
        }
      }
      else if (in["action"] == "reshape") {
        if (fNavigator) {
          SafeDelete(fNavigator);
        }

        std::vector<std::vector<int>> levels = {{0, 1}};
        if (in.contains("levels")) {
          levels = in["levels"].get<std::vector<std::vector<int>>>();
        }

        fNavigator = fNGnTree->Reshape("", levels);
        if (fNavigator) {
          fNavigator->Print();
          out["result"] = "success";
        }
        else {
          out["result"] = "failure";
        }
      }
      else if (in["action"] == "spectra") {

        if (fNavigator) {
          fNavigator->Print();
          fNavigator->Draw("HOVER");
          out["result"] = "success";
        }
        else {
          out["result"] = "failure";
        }
      }
      else {
        NLogWarning("Unknown action: %s", in["action"].get<std::string>().c_str());
        out["error"] = "Unknown action";
      }
    }
  }
  else if (method == "PATCH") {
    NLogInfo("Processing PATCH request");
  }
  else if (method == "OPTIONS") {
    NLogInfo("Processing OPTIONS request");
  }
  else if (method == "DELETE") {
    NLogInfo("Processing DELETE request");
  }
  else {
    NLogWarning("Unsupported HTTP method: %s", method.Data());
    out["error"] = "Unsupported HTTP method ";
    arg->SetContentType("application/json");
    arg->SetContent(out.dump());
    return;
  }

  out["status"] = "ok";

  arg->AddHeader("X-Header", "Test");
  arg->SetContentType("application/json");
  arg->SetContent(out.dump());
  // arg->SetContent("ok");
  // arg->SetContentType("text/plain");
}
} // namespace Ndmspc
