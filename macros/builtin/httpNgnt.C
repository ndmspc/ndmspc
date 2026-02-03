#include <exception>
#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>
#include <NLogger.h>

void httpNgnt()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // Store lambdas (must be non-capturing to convert to function pointer)
  handlers["open"] = [](std::string method, json & in, json & out, std::map<std::string, TObject *> & inputs) {
    NLogInfo("HTTP method called: %s", method.c_str());
    NLogInfo("Processing NGnTree open request");
    Ndmspc::NGnTree * ngnt = (Ndmspc::NGnTree *)nullptr;
    if (inputs.find("ngnt") != inputs.end()) {
      ngnt = (Ndmspc::NGnTree *)inputs["ngnt"];
    }

    if (method.find("GET") != std::string::npos) {
      if (ngnt && !ngnt->IsZombie()) {
        NLogInfo("NGnTree is already opened");
        out["result"]      = "success";
        out["file"]        = ngnt->GetStorageTree()->GetFileName();
        out["treename"]    = ngnt->GetStorageTree()->GetTree()->GetName();
        out["nEntries"]    = ngnt->GetStorageTree()->GetTree()->GetEntries();
        out["nDimensions"] = ngnt->GetBinning()->GetAxes().size();
        std::vector<std::string> axisNames;
        for (auto axis : ngnt->GetBinning()->GetAxes()) {
          axisNames.push_back(axis->GetName());
        }
        out["axes"]                  = axisNames;
        out["branches"]              = ngnt->GetStorageTree()->GetBrancheNames();
        Ndmspc::NParameters * params = ngnt->GetParameters();
        out["parameters"]            = params ? params->GetNames() : std::vector<std::string>{};
        // inputs["ngnt"] = ngnt;
      }
      else {
        NLogInfo("NGnTree is not opened");
        out["result"] = "not_opened";
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (in.contains("file")) {
        std::string file = in["file"].get<std::string>();
        NLogInfo("Opening NGnTree from file: %s", file.c_str());
        if (ngnt && !ngnt->IsZombie()) {

          if (file == ngnt->GetStorageTree()->GetFileName()) {
            NLogInfo("NGnTree already opened from file: %s", file.c_str());
            out["result"] = "success";
            // inputs["ngnt"] = ngnt;
            return;
          }
          inputs.erase("ngnt");

          delete ngnt;
          ngnt = nullptr;
        }
        ngnt = Ndmspc::NGnTree::Open(file);
        if (ngnt) {
          NLogInfo("Successfully opened NGnTree from file: %s", file.c_str());
          out["result"]  = "success";
          inputs["ngnt"] = ngnt;
        }
        else {
          NLogError("Failed to open NGnTree from file: %s", file.c_str());
          out["result"] = "failure";
        }
      }
      else {
        out["error"] = "Missing 'file' parameter for open action";
      }
    }
    else {
      out["error"] = "Unsupported HTTP method for open action";
    }
  };
  handlers["reshape"] = [](std::string method, json & in, json & out, std::map<std::string, TObject *> & inputs) {
    NLogInfo("HTTP method called: %s", method.c_str());
    Ndmspc::NGnTree * ngnt = (Ndmspc::NGnTree *)nullptr;
    if (inputs.find("ngnt") != inputs.end()) {
      ngnt = (Ndmspc::NGnTree *)inputs["ngnt"];
    }

    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened, cannot reshape");
      out["result"] = "not_opened";
      return;
    }

    Ndmspc::NGnNavigator * nav = (Ndmspc::NGnNavigator *)nullptr;
    if (inputs.find("navigator") != inputs.end()) {
      nav = (Ndmspc::NGnNavigator *)inputs["navigator"];
    }

    if (method.find("GET") != std::string::npos) {
      if (nav) {
        NLogInfo("Reshape navigator is available");
        nav->Print();
        out["info"]   = nav->GetInfoJson();
        out["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        out["result"] = "not_available";
        return;
      }
    }
    else if (method.find("POST") != std::string::npos) {
      std::string binningName = "";
      if (in.contains("binningName")) {
        binningName = in["binningName"].get<std::string>();
      }

      std::vector<std::vector<int>> levels;
      if (in.contains("levels")) {
        levels = in["levels"].get<std::vector<std::vector<int>>>();
      }

      if (nav) {
        SafeDelete(nav);
        nav = nullptr;
      }
      inputs["navigator"] = (TObject *)ngnt->Reshape("", levels, 0, {}, {});
      if (inputs["navigator"]) {
        out["result"] = "success";
      }
      else {
        out["result"] = "failure";
      }
    }
    else {
      out["error"] = "Unsupported HTTP method for reshape action";
    }
  };

  handlers["point"] = [](std::string method, json & in, json & out, std::map<std::string, TObject *> & inputs) {
    NLogInfo("point: HTTP method called: %s", method.c_str());
    Ndmspc::NGnNavigator * nav = (Ndmspc::NGnNavigator *)nullptr;
    if (inputs.find("navigator") != inputs.end()) {
      nav = (Ndmspc::NGnNavigator *)inputs["navigator"];
    }

    Ndmspc::NGnTree * ngnt = (Ndmspc::NGnTree *)nullptr;
    if (inputs.find("ngnt") != inputs.end()) {
      ngnt = (Ndmspc::NGnTree *)inputs["ngnt"];
    }

    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened");
      out["result"] = "not_opened";
      return;
    }

    Ndmspc::NGnHttpServer * server = (Ndmspc::NGnHttpServer *)nullptr;
    if (inputs.find("httpServer") != inputs.end()) {
      server = (Ndmspc::NGnHttpServer *)inputs["httpServer"];
    }

    if (method.find("GET") != std::string::npos) {
      if (nav) {
        NLogInfo("Point navigator is available");
        // nav->Draw("hover");

        TH1 *   proj = nav->GetProjection();
        TString h    = TBufferJSON::ConvertToJSON(proj);
        json    hMap = json::parse(h.Data());
        json    wsOut;
        wsOut["map"]["obj"] = hMap;
        json clickAction;
        json clickHttp                    = clickAction["http"];
        clickHttp["method"]               = "GET";
        clickHttp["contentType"]          = "application/json";
        clickHttp["path"]                 = "point";
        clickHttp["payload"]              = json::object();
        wsOut["map"]["handlers"]["click"] = clickAction;

        // wsOut["map"]["handlers"]["hover"]["action"]       = "contenthover";
        if (!server) {
          NLogError("HTTP server is not available, cannot publish navigator");
          out["result"] = "http_server_not_available";
          return;
        }
        server->WebSocketBroadcast(wsOut);

        out["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        out["result"] = "not_available";
        return;
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (nav) {
        NLogInfo("Reshape navigator is available");
        // nav->Draw("hover");
        //
        int entry = in.contains("entry") ? in["entry"].get<int>() : -1;

        json wsOut;
        if (entry >= 0) {
          ngnt->GetEntry(entry);
          TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("outputPoint");
          if (outputPoint) {
            NLogInfo("Output point for bin %d:", entry);
            // outputPoint->Print();
            wsOut["content"] = json::parse(TBufferJSON::ConvertToJSON(outputPoint).Data());
          }
          else {
            NLogWarning("No output point found for entry %d", entry);
          }
        }

        if (!server) {
          NLogError("HTTP server is not available, cannot publish navigator");
          out["result"] = "http_server_not_available";
          return;
        }
        server->WebSocketBroadcast(wsOut);

        out["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        out["result"] = "not_available";
        return;
      }
    }
    else {
      out["error"] = "Unsupported HTTP method for reshape action";
    }
  };

  handlers["spectra"] = [](std::string method, json & in, json & out, std::map<std::string, TObject *> & inputs) {
    NLogInfo("publich: HTTP method called: %s", method.c_str());
    Ndmspc::NGnNavigator * nav = (Ndmspc::NGnNavigator *)nullptr;
    if (inputs.find("navigator") != inputs.end()) {
      nav = (Ndmspc::NGnNavigator *)inputs["navigator"];
    }

    Ndmspc::NGnTree * ngnt = (Ndmspc::NGnTree *)nullptr;
    if (inputs.find("ngnt") != inputs.end()) {
      ngnt = (Ndmspc::NGnTree *)inputs["ngnt"];
    }

    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened");
      out["result"] = "not_opened";
      return;
    }

    Ndmspc::NGnHttpServer * server = (Ndmspc::NGnHttpServer *)nullptr;
    if (inputs.find("httpServer") != inputs.end()) {
      server = (Ndmspc::NGnHttpServer *)inputs["httpServer"];
    }
    if (method.find("GET") != std::string::npos) {
    }
    else if (method.find("POST") != std::string::npos) {
      if (nav) {
        NLogInfo("Reshape navigator is available");
        // nav->Draw("hover");
        //
        std::string         parameterName = in.contains("parameterName") ? in["parameterName"].get<std::string>() : "";
        std::vector<double> minmax;
        if (in.contains("minmax")) {
          minmax = in["minmax"].get<std::vector<double>>();
        }

        json    wsOut;
        TList * spectra = nav->DrawSpectraAll(parameterName, minmax, "");
        if (spectra) {
          NLogInfo("Spectra for parameter '%s' obtained:", parameterName.c_str());
          // spectra->Print();
          wsOut["spectra"] = json::parse(TBufferJSON::ConvertToJSON(spectra).Data());
          if (!server) {
            NLogError("HTTP server is not available, cannot publish navigator");
            out["result"] = "http_server_not_available";
            return;
          }
          server->WebSocketBroadcast(wsOut);
        }
        else {
          NLogWarning("No spectra found for parameter '%s'", parameterName.c_str());
        }

        out["result"] = "success";
      }
      else {
        NLogInfo("Reshape navigator is not available");
        out["result"] = "not_available";
        return;
      }
    }
    else {
      out["error"] = "Unsupported HTTP method for reshape action";
    }
  };
}
