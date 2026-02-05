#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NUtils.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>
#include <NLogger.h>

void httpNgnt()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // Store lambdas (must be non-capturing to convert to function pointer)
  handlers["open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                        std::map<std::string, TObject *> &) {
    auto              server = Ndmspc::gNGnHttpServer;
    Ndmspc::NGnTree * ngnt   = (Ndmspc::NGnTree *)server->GetInputObject("ngnt");

    if (method.find("GET") != std::string::npos) {
      if (ngnt && !ngnt->IsZombie()) {
        NLogInfo("NGnTree is already opened");
        httpOut["result"]      = "success";
        httpOut["file"]        = ngnt->GetStorageTree()->GetFileName();
        httpOut["treename"]    = ngnt->GetStorageTree()->GetTree()->GetName();
        httpOut["nEntries"]    = ngnt->GetStorageTree()->GetTree()->GetEntries();
        httpOut["nDimensions"] = ngnt->GetBinning()->GetAxes().size();
        std::vector<std::string> axisNames;
        for (auto axis : ngnt->GetBinning()->GetAxes()) {
          axisNames.push_back(axis->GetName());
        }
        httpOut["axes"]              = axisNames;
        httpOut["branches"]          = ngnt->GetStorageTree()->GetBrancheNames();
        Ndmspc::NParameters * params = ngnt->GetParameters();
        httpOut["parameters"]        = params ? params->GetNames() : std::vector<std::string>{};
        // inputs["ngnt"] = ngnt;
      }
      else {
        NLogInfo("NGnTree is not opened");
        httpOut["result"] = "not_opened";
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (httpIn.contains("file")) {
        std::string file = httpIn["file"].get<std::string>();
        NLogInfo("Opening NGnTree from file: %s", file.c_str());
        if (ngnt && !ngnt->IsZombie()) {

          if (file == ngnt->GetStorageTree()->GetFileName()) {
            NLogInfo("NGnTree already opened from file: %s", file.c_str());
            httpOut["result"] = "success";
            // inputs["ngnt"] = ngnt;
            return;
          }
          server->RemoveInputObject("ngnt");
        }
        ngnt = Ndmspc::NGnTree::Open(file);
        if (ngnt) {
          NLogInfo("Successfully opened NGnTree from file: %s", file.c_str());
          httpOut["result"]       = "success";
          wsOut["ui"]["filename"] = file;
          // server->WebSocketBroadcast(wsOut);
          server->AddInputObject("ngnt", ngnt);
        }
        else {
          NLogError("Failed to open NGnTree from file: %s", file.c_str());
          httpOut["result"] = "failure";
        }
      }
      else {
        httpOut["error"] = "Missing 'file' parameter for open action";
      }
    }
    else if (method.find("DELETE") != std::string::npos) {

      if (ngnt) {
        NLogInfo("Closing NGnTree %s", ngnt->GetStorageTree()->GetFileName().c_str());
        server->RemoveInputObject("ngnt");
      }
      else {
        NLogInfo("No NGnTree to close");
      }
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for open action";
    }
  };
  handlers["reshape"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                           std::map<std::string, TObject *> &) {
    auto              server = Ndmspc::gNGnHttpServer;
    Ndmspc::NGnTree * ngnt   = (Ndmspc::NGnTree *)server->GetInputObject("ngnt");
    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened, cannot reshape");
      httpOut["result"] = "not_opened";
      return;
    }

    Ndmspc::NGnNavigator * nav = (Ndmspc::NGnNavigator *)server->GetInputObject("navigator");

    if (method.find("GET") != std::string::npos) {
      if (nav) {
        NLogInfo("Reshape navigator is available");
        nav->Print();
        httpOut["info"]   = nav->GetInfoJson();
        httpOut["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else if (method.find("POST") != std::string::npos) {
      std::string binningName = "";
      if (httpIn.contains("binningName")) {
        binningName = httpIn["binningName"].get<std::string>();
      }

      std::vector<std::vector<int>> levels;
      if (httpIn.contains("levels")) {
        levels = httpIn["levels"].get<std::vector<std::vector<int>>>();
      }

      if (nav) {
        SafeDelete(nav);
        nav = nullptr;
      }
      nav = ngnt->Reshape("", levels, 0, {}, {});
      server->AddInputObject("navigator", nav);
      if (nav) {
        httpOut["result"]          = "success";
        wsOut["ui"]["binningName"] = binningName.empty() ? ngnt->GetBinning()->GetCurrentDefinitionName() : binningName;
        wsOut["ui"]["binnings"]    = ngnt->GetBinning()->GetDefinitionNames();
        wsOut["ui"]["levels"]      = levels;
      }
      else {
        httpOut["result"] = "failure";
      }
    }
    else if (method.find("DELETE") != std::string::npos) {

      NLogInfo("[DELETE][reshape] Closing reshape navigator %p", (void *)nav);
      if (nav) {
        NLogInfo("[DELETE][reshape] Navigator deleted successfully");
        server->RemoveInputObject("navigator");
      }
      else {
        NLogInfo("No NGnTree to close");
      }
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for reshape action";
    }
  };

  handlers["point"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                         std::map<std::string, TObject *> &) {
    auto              server = Ndmspc::gNGnHttpServer;
    Ndmspc::NGnTree * ngnt   = (Ndmspc::NGnTree *)server->GetInputObject("ngnt");
    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened");
      httpOut["result"] = "NGnTree is not opened";
      return;
    }

    Ndmspc::NGnNavigator * nav = (Ndmspc::NGnNavigator *)server->GetInputObject("navigator");
    if (!nav) {
      NLogError("Navigator is not available");
      httpOut["result"] = "navigator_not_available";
      return;
    }

    if (method.find("GET") != std::string::npos) {
      if (nav) {
        NLogInfo("Point navigator is available");
        // nav->Draw("hover");

        TH1 *   proj        = nav->GetProjection();
        TString h           = TBufferJSON::ConvertToJSON(proj);
        json    hMap        = json::parse(h.Data());
        wsOut["map"]["obj"] = hMap;
        json clickAction;
        clickAction["type"]               = "http";
        clickAction["method"]             = "GET";
        clickAction["contentType"]        = "application/json";
        clickAction["path"]               = "point";
        clickAction["payload"]            = json::object();
        wsOut["map"]["handlers"]["click"] = clickAction;

        // wsOut["map"]["handlers"]["hover"]["action"]       = "contenthover";
        // server->WebSocketBroadcast(wsOut);

        httpOut["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (nav) {
        NLogInfo("Reshape navigator is available");
        // nav->Draw("hover");
        //
        int entry = httpIn.contains("entry") ? httpIn["entry"].get<int>() : -1;

        if (entry >= 0) {
          ngnt->GetEntry(entry);
          TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("outputPoint");
          if (outputPoint) {
            NLogInfo("Output point for bin %d:", entry);
            outputPoint->ls();
            wsOut["content"] = json::parse(TBufferJSON::ConvertToJSON(outputPoint).Data());
          }
          else {
            NLogWarning("No output point found for entry %d", entry);
          }
        }

        httpOut["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for reshape action";
    }
  };

  handlers["spectra"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                           std::map<std::string, TObject *> &) {
    auto              server = Ndmspc::gNGnHttpServer;
    Ndmspc::NGnTree * ngnt   = (Ndmspc::NGnTree *)server->GetInputObject("ngnt");
    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened");
      httpOut["result"] = "NGnTree is not opened";
      return;
    }

    Ndmspc::NGnNavigator * nav = (Ndmspc::NGnNavigator *)server->GetInputObject("navigator");
    if (!nav) {
      NLogError("Navigator is not available");
      httpOut["result"] = "navigator_not_available";
      return;
    }

    if (method.find("GET") != std::string::npos) {
    }
    else if (method.find("POST") != std::string::npos) {
      if (nav) {
        NLogInfo("Reshape navigator is available");
        // nav->Draw("hover");
        //
        std::string         parameterName = httpIn.contains("parameter") ? httpIn["parameter"].get<std::string>() : "";
        std::vector<double> minmax;
        if (httpIn.contains("minmax")) {
          minmax = httpIn["minmax"].get<std::vector<double>>();
        }

        TList * spectra = nav->DrawSpectraAll(parameterName, minmax, "");
        if (spectra) {
          NLogInfo("Spectra for parameter '%s' obtained:", parameterName.c_str());
          // spectra->Print();
          wsOut["spectra"] = json::parse(TBufferJSON::ConvertToJSON(spectra).Data());
          if (!server) {
            NLogError("HTTP server is not available, cannot publish navigator");
            httpOut["result"] = "http_server_not_available";
            return;
          }
          server->WebSocketBroadcast(wsOut);
        }
        else {
          NLogWarning("No spectra found for parameter '%s'", parameterName.c_str());
        }

        httpOut["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for reshape action";
    }
  };
}
