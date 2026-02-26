#include <algorithm>
#include <map>
#include <string>
#include <TBufferJSON.h>
#include <NUtils.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NGnHttpServer.h>

void httpNgnt()
{

  auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);

  handlers["open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                        std::map<std::string, TObject *> &) {
    auto              server = Ndmspc::gNGnHttpServer;
    Ndmspc::NGnTree * ngnt   = (Ndmspc::NGnTree *)server->GetInputObject("ngnt");

    if (method.find("GET") != std::string::npos) {
      if (ngnt && !ngnt->IsZombie()) {
        NLogTrace("NGnTree is already opened");
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
        NLogTrace("NGnTree is not opened");
        httpOut["result"] = "not_opened";
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (httpIn.contains("file")) {
        std::string file = httpIn["file"].get<std::string>();
        NLogTrace("Opening NGnTree from file: %s", file.c_str());
        if (ngnt && !ngnt->IsZombie()) {

          if (file == ngnt->GetStorageTree()->GetFileName()) {
            NLogTrace("NGnTree already opened from file: %s", file.c_str());
            httpOut["result"] = "success";
            // inputs["ngnt"] = ngnt;
            return;
          }
          server->RemoveInputObject("ngnt");
        }
        ngnt = Ndmspc::NGnTree::Open(file);
        if (ngnt) {
          NLogTrace("Successfully opened NGnTree from file: %s", file.c_str());
          httpOut["result"]                                               = "success";
          server->GetWorkspace()["open"]["type"]                          = "object";
          server->GetWorkspace()["open"]["properties"]["file"]["type"]    = "string";
          server->GetWorkspace()["open"]["properties"]["file"]["default"] = file;
          wsOut["workspace"]["open"]                                      = server->GetWorkspace()["open"];

          if (server->GetWorkspace().contains("reshape") == false) {

            // Build reshape properties based on NGnTree binning (default values and options for levels and binning
            // definitions)
            json reshapeProperties;
            reshapeProperties["levels"]["type"]        = "array";
            reshapeProperties["levels"]["description"] = "A nested array of integers representing levels.";

            // Build nested array based on actual axes count
            json      default_levels         = json::array();
            size_t    nAxes                  = ngnt->GetBinning()->GetAxes().size();
            const int MAX_ELEMENTS_PER_LEVEL = 3;
            for (size_t i = 0; i < nAxes; i += MAX_ELEMENTS_PER_LEVEL) {
              json level = json::array();
              for (int j = 0; j < MAX_ELEMENTS_PER_LEVEL && (i + j) < nAxes; j++) {
                level.push_back(static_cast<int>(i + j));
              }
              default_levels.push_back(level);
            }
            reshapeProperties["levels"]["default"]                = default_levels;
            reshapeProperties["levels"]["items"]["type"]          = "array";
            reshapeProperties["levels"]["items"]["items"]["type"] = "integer";

            std::vector<std::string> binningNames       = ngnt->GetBinning()->GetDefinitionNames();
            std::string              currentBinningName = ngnt->GetBinning()->GetCurrentDefinitionName();
            if (currentBinningName.empty() && !binningNames.empty()) {
              currentBinningName = binningNames.front();
            }
            else if (!binningNames.empty() &&
                     std::find(binningNames.begin(), binningNames.end(), currentBinningName) == binningNames.end()) {
              currentBinningName = binningNames.front();
            }
            reshapeProperties["binningName"]["type"]    = "string";
            reshapeProperties["binningName"]["format"]  = "select";
            reshapeProperties["binningName"]["enum"]    = binningNames;
            reshapeProperties["binningName"]["default"] = currentBinningName;

            // wsOut["workspace"]["reshape"]["properties"] = reshapeProperties;
            server->GetWorkspace()["reshape"]["properties"] = reshapeProperties;
            server->GetWorkspace()["reshape"]["type"]       = "object";
          }
          wsOut["workspace"]["reshape"] = server->GetWorkspace()["reshape"];

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
        NLogTrace("Closing NGnTree %s", ngnt->GetStorageTree()->GetFileName().c_str());
        server->RemoveInputObject("ngnt");
      }
      else {
        NLogTrace("No NGnTree to close");
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
        NLogTrace("Reshape navigator is available");
        nav->Print();
        httpOut["info"]   = nav->GetInfoJson();
        httpOut["result"] = "success";
      }

      else {
        NLogTrace("Reshape navigator is not available");
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

      if (nav) {
        server->AddInputObject("navigator", nav);
        server->GetWorkspace()["reshape"]["properties"]["levels"]["default"]      = levels;
        server->GetWorkspace()["reshape"]["properties"]["binningName"]["default"] = binningName;
        wsOut["workspace"]["reshape"]                                             = server->GetWorkspace()["reshape"];

        if (server->GetWorkspace().contains("map") == false) {
          // set default mapping and content pads for map workspace
          json mapProperties;
          mapProperties["mappingPad"]["type"]    = "string";
          mapProperties["mappingPad"]["default"] = "pad1";
          mapProperties["contentPad"]["type"]    = "string";
          mapProperties["contentPad"]["default"] = "pad2";

          server->GetWorkspace()["map"]["properties"] = mapProperties;
          server->GetWorkspace()["map"]["type"]       = "object";
        }
        wsOut["workspace"]["map"] = server->GetWorkspace()["map"];

        httpOut["result"] = "success";
      }
      else {
        httpOut["result"] = "failure";
      }
    }
    else if (method.find("DELETE") != std::string::npos) {

      NLogTrace("[DELETE][reshape] Closing reshape navigator %p", (void *)nav);
      if (nav) {
        NLogTrace("[DELETE][reshape] Navigator deleted successfully");
        server->RemoveInputObject("navigator");
      }
      else {
        NLogTrace("No NGnTree to close");
      }
      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for reshape action";
    }
  };

  handlers["map"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
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

    if (method.find("POST") != std::string::npos) {
      std::string mappingPad = "";
      if (httpIn.contains("mappingPad")) {
        mappingPad = httpIn["mappingPad"].get<std::string>();
      }
      if (mappingPad.empty()) {
        mappingPad = "pad1";
      }

      std::string contentPad = "";
      if (httpIn.contains("contentPad")) {
        contentPad = httpIn["contentPad"].get<std::string>();
      }
      if (contentPad.empty()) {
        contentPad = "pad2";
      }

      // Print all pads
      NLogDebug("Mapping pad: %s, Content pad: %s", mappingPad.c_str(), contentPad.c_str());

      TList * l    = new TList();
      TH1 *   proj = nav->GetProjection();

      proj->SetStats(false);
      l->Add(proj);

      TString listStr  = TBufferJSON::ConvertToJSON(l);
      json    listJson = json::parse(listStr.Data());

      // loop listJson and add clickAction to each histogram
      std::vector<int> pointForClickAction;

      for (auto & item : listJson["arr"]) {

        json clickAction;
        clickAction["type"]             = "http";
        clickAction["method"]           = "PATCH";
        clickAction["path"]             = "map";
        clickAction["contentType"]      = "application/json";
        clickAction["payload"]          = json::object();
        clickAction["payload"]["point"] = json::array();

        json debugAction;
        debugAction["type"]    = "debug";
        debugAction["message"] = std::string("Debug click: ") + item["fName"].dump();

        json clicks = json::array({clickAction, debugAction});

        NLogDebug("[Server] map POST XXX level %d == %d - 2", nav->GetLevel(), nav->GetNLevels());
        if (nav->GetLevel() == nav->GetNLevels() - 2) {
          json clickSpectra;
          clickSpectra["type"]             = "http";
          clickSpectra["method"]           = "PATCH";
          clickSpectra["path"]             = "spectra";
          clickSpectra["contentType"]      = "application/json";
          clickSpectra["payload"]          = json::object();
          clickSpectra["payload"]["point"] = pointForClickAction;
          NLogDebug("[Server] Adding click handler for spectra at final level navigator, point: %s",
                    json(listJson["arr"][0]).dump().c_str());
          clicks.push_back(clickSpectra);
        }
        item["handlers"]["click"] = clicks;
      }

      wsOut["payload"]["map"]["obj"]        = listJson;
      wsOut["payload"]["map"]["targetPad"]  = mappingPad;
      wsOut["payload"]["map"]["contentPad"] = contentPad;

      server->GetWorkspace()["map"]["properties"]["mappingPad"]["default"] = mappingPad;
      server->GetWorkspace()["map"]["properties"]["contentPad"]["default"] = contentPad;
      wsOut["workspace"]["map"]                                            = server->GetWorkspace()["map"];

      if (server->GetWorkspace().contains("spectra") == false) {

        json spectraProperties;
        spectraProperties["startPad"]["type"]    = "string";
        spectraProperties["startPad"]["default"] = "pad3";

        spectraProperties["parameters"]["type"]          = "array";
        spectraProperties["parameters"]["format"]        = "multiselect";
        spectraProperties["parameters"]["items"]["type"] = "string";
        auto paramNames                                  = nav->GetParameterNames();
        spectraProperties["parameters"]["items"]["enum"] = paramNames;
        if (!paramNames.empty()) {
          spectraProperties["parameters"]["default"] = json::array({paramNames.front()});
        }
        else {
          spectraProperties["parameters"]["default"] = json::array();
        }

        spectraProperties["minmaxMode"]["type"]         = "string";
        spectraProperties["minmaxMode"]["default"]      = "D";
        spectraProperties["minmaxMode"]["format"]       = "select";
        spectraProperties["minmaxMode"]["enum"]         = {"D", "V", "VE"};
        spectraProperties["axismargin"]["type"]         = "number";
        spectraProperties["axismargin"]["default"]      = 0.05;
        server->GetWorkspace()["spectra"]["properties"] = spectraProperties;
        server->GetWorkspace()["spectra"]["type"]       = "object";
      }
      wsOut["workspace"]["spectra"] = server->GetWorkspace()["spectra"];

      httpOut["result"] = "success";
    }
    else if (method.find("PATCH") != std::string::npos) {
      NLogTrace("[Server] PATCH map received: %s", httpIn.dump().c_str());

      std::vector<int> point;
      if (httpIn.contains("point")) {
        point = httpIn["point"].get<std::vector<int>>();
        NLogTrace("[Server] PATCH map received point: %s", json(point).dump().c_str());
      }

      json binInfo;
      if (httpIn.contains("args")) {
        binInfo = httpIn["args"];
        int bin = binInfo["bin"].get<int>();
        point.push_back(bin);
        NLogTrace("[Server] PATCH map added bin to point: %d", bin);
      }

      NLogTrace("[Server] Final point for PATCH map: %s", json(point).dump().c_str());

      Ndmspc::NGnNavigator * navCurrent = nav;
      for (const auto & bin : point) {
        NLogTrace("[Server] Traversing to child navigator for bin: %d navCurrent=%p", bin, (void *)navCurrent);
        navCurrent = navCurrent->GetChild(bin);
      }

      NLogTrace("[Server] Final navigator after traversal: %p", (void *)navCurrent);

      if (navCurrent && navCurrent->GetChildren().size() > 0) {
        TH1 * proj = navCurrent->GetProjection();
        proj->SetStats(false);
        // Always wrap in a TList for UI consistency
        TList l;
        l.Add(proj);
        TString          listStr             = TBufferJSON::ConvertToJSON(&l);
        json             listJson            = json::parse(listStr.Data());
        std::vector<int> pointForClickAction = point;
        for (auto & item : listJson["arr"]) {
          json clickMap;
          clickMap["type"]             = "http";
          clickMap["method"]           = "PATCH";
          clickMap["path"]             = "map";
          clickMap["contentType"]      = "application/json";
          clickMap["payload"]          = json::object();
          clickMap["payload"]["point"] = pointForClickAction;
          json clicks                  = json::array({clickMap});

          NLogDebug("[Server] XXX level %d == %d - 2", navCurrent->GetLevel(), nav->GetNLevels());
          if (navCurrent->GetLevel() == nav->GetNLevels() - 2) {
            json clickSpectra;
            clickSpectra["type"]             = "http";
            clickSpectra["method"]           = "PATCH";
            clickSpectra["path"]             = "spectra";
            clickSpectra["contentType"]      = "application/json";
            clickSpectra["payload"]          = json::object();
            clickSpectra["payload"]["point"] = pointForClickAction;
            NLogDebug("[Server] Adding click handler for spectra at final level navigator, point: %s",
                      json(listJson["arr"][0]).dump().c_str());
            clicks.push_back(clickSpectra);
          }
          item["handlers"]["click"] = clicks;
          NLogTrace("[Server] Sending only new projection for PATCH, point: %s",
                    json(pointForClickAction).dump().c_str());
        }
        // Always send as TList, even for single histogram
        wsOut["payload"]["map"]["obj"]         = listJson;
        wsOut["payload"]["map"]["appendToTab"] = true;
        wsOut["payload"]["map"]["targetPad"]   = httpIn.contains("mappingPad") ? httpIn["mappingPad"] : "pad1";
        // wsOut["payload"]["map"]["contentPad"]  = httpIn.contains("contentPad") ? httpIn["contentPad"] : "pad3";

        if (navCurrent->GetLevel() == nav->GetNLevels() - 1) {
          NLogDebug("[Server] Reached final level navigator, sending content for point: %s [%d/%d]",
                    json(pointForClickAction).dump().c_str(), navCurrent->GetLevel(), navCurrent->GetNLevels());

          // check of server workspace spectra parameters is empty or different from current navigator parameters, if so
          // update it and send to client

          // check if workspace spectra parameters are missing or empty, if so set it based on current navigator
          // parameters
          if (!server->GetWorkspace().contains("spectra") ||
              !server->GetWorkspace()["spectra"].contains("properties") ||
              !server->GetWorkspace()["spectra"]["properties"].contains("parameters") ||
              !server->GetWorkspace()["spectra"]["properties"]["parameters"].contains("items") ||
              !server->GetWorkspace()["spectra"]["properties"]["parameters"]["items"].contains("enum") ||
              server->GetWorkspace()["spectra"]["properties"]["parameters"]["items"]["enum"].empty()) {

            if (!navCurrent->GetParameterNames().empty()) {
              NLogDebug("[Server] Updating spectra parameters for final level navigator: %s",
                        json(navCurrent->GetParameterNames()).dump().c_str());
              server->GetWorkspace()["spectra"]["properties"]["parameters"]["items"]["enum"] =
                  navCurrent->GetParameterNames();
              server->GetWorkspace()["spectra"]["properties"]["parameters"]["default"] =
                  navCurrent->GetParameterNames().empty() ? json::array()
                                                          : json::array({navCurrent->GetParameterNames().front()});
              NLogDebug("[Server] Updated spectra parameters for final level navigator: %s",
                        json(navCurrent->GetParameterNames()).dump().c_str());

              wsOut["payload"]["workspace"]["spectra"] = server->GetWorkspace()["spectra"];
            }
          }
        }
      }
      else {
        NLogTrace("[Server] No projection found at final level, nothing sent to websocket");
        // Do not send anything to websocket if navCurrent is not found
        int entry = httpIn.contains("entry") ? httpIn["entry"].get<int>() : -1;
        if (entry >= 0) {
          ngnt->GetEntry(entry);
          TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("outputPoint");
          if (outputPoint) {
            NLogTrace("Output point for bin %d:", entry);
            // outputPoint->ls();
            wsOut["payload"]["content"]              = json::parse(TBufferJSON::ConvertToJSON(outputPoint).Data());
            wsOut["payload"]["content"]["targetPad"] = httpIn.contains("contentPad") ? httpIn["contentPad"] : "pad3";
          }
          else {
            NLogTrace("No output point found for entry %d", entry);
            httpOut["result"] = "skipped";
            return;
          }
        }
        else {
          NLogTrace("[Server] No entry provided for PATCH map and no projection found, nothing sent to websocket");
          httpOut["result"] = "skipped";
          return;
        }
      }

      httpOut["result"] = "success";
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for map action";
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
      std::string spectraPad = "";
      if (httpIn.contains("startPad")) {
        spectraPad = httpIn["startPad"].get<std::string>();
      }
      if (spectraPad.empty()) {
        spectraPad = "pad3";
      }

      if (nav) {
        NLogTrace("Reshape navigator is available");
        // nav->Draw("hover");
        //
        std::vector<std::string> parameterName = httpIn.contains("parameters")
                                                     ? httpIn["parameters"].get<std::vector<std::string>>()
                                                     : std::vector<std::string>{};

        if (parameterName.empty()) {
          NLogWarning("No parameter name provided for spectra !!!");
          httpOut["result"] = "missing_parameters";
          return;
        }

        double minmax = 0.05;
        if (httpIn.contains("axismargin")) {
          minmax = {httpIn["axismargin"].get<double>()};
        }
        std::string minmaxMode = "V";
        if (httpIn.contains("minmaxMode")) {
          minmaxMode = httpIn["minmaxMode"].get<std::string>();
        }

        server->GetWorkspace()["spectra"]["properties"]["startPad"]["default"]   = spectraPad;
        server->GetWorkspace()["spectra"]["properties"]["parameters"]["default"] = parameterName;

        // server->GetWorkspace()["spectra"]["properties"]["parameters"]["default"] = parameterName;
        // Only use the first element for DrawSpectra and workspace default
        // server->GetWorkspace()["spectra"]["properties"]["axismargin"]["default"] = minmax;
        server->GetWorkspace()["spectra"]["properties"]["minmaxMode"]["default"] = minmaxMode;
        wsOut["workspace"]["spectra"]                                            = server->GetWorkspace()["spectra"];

        // Parse starting pad index from pad name (e.g., pad4 → 4)
        int padIndex = 3;
        if (spectraPad.size() > 3 && spectraPad.substr(0, 3) == "pad") {
          try {
            padIndex = std::stoi(spectraPad.substr(3));
          }
          catch (...) {
            padIndex = 3;
          }
        }
        std::vector<json> multiSpectra;
        for (const auto & param : parameterName) {
          std::string padName = "pad" + std::to_string(padIndex);
          TList *     spectra = nav->DrawSpectraAll(param, {minmax}, minmaxMode, "");
          if (spectra) {
            NLogTrace("Spectra for parameter '%s' obtained:", param.c_str());
            json spectraObject = json::parse(TBufferJSON::ConvertToJSON(spectra).Data());
            json debugAction;
            debugAction["type"]                = "debug";
            debugAction["message"]             = std::string("Debug click: ") + spectraObject["fName"].dump();
            spectraObject["handlers"]["click"] = json::array({debugAction});
            spectraObject["targetPad"]         = padName;
            spectraObject["parameter"]         = param;
            multiSpectra.push_back(spectraObject);
          }
          else {
            NLogWarning("No spectra found for parameter '%s'", param.c_str());
          }
          padIndex++;
        }
        if (!multiSpectra.empty()) {
          wsOut["payload"]["spectra"]["objs"]       = multiSpectra;
          wsOut["payload"]["spectra"]["parameters"] = parameterName;
          wsOut["payload"]["spectra"]["multipad"]   = true;
        }
        wsOut["payload"]["spectra"]["axismargin"] = minmax;
        if (!minmaxMode.empty()) {
          wsOut["payload"]["spectra"]["minmaxMode"] = minmaxMode;
        }

        httpOut["result"] = "success";
      }
      else {
        NLogTrace("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else if (method.find("PATCH") != std::string::npos) {

      NLogDebug("[Server] PATCH spectra received: %s", httpIn.dump().c_str());
      std::vector<int> point;
      if (httpIn.contains("point")) {
        point = httpIn["point"].get<std::vector<int>>();
        NLogTrace("[Server] PATCH map received point: %s", json(point).dump().c_str());
      }
      int newBin = -1;
      if (httpIn.contains("args")) {
        json binInfo = httpIn["args"];
        newBin       = binInfo["bin"].get<int>();
      }
      point.push_back(newBin);
      NLogDebug("[Server] Final point for PATCH spectra: %s", json(point).dump().c_str());

      Ndmspc::NGnNavigator * navCurrent = nav;
      for (const auto & bin : point) {
        NLogTrace("[Server] Traversing to child navigator for bin: %d navCurrent=%p", bin, (void *)navCurrent);
        navCurrent = navCurrent->GetChild(bin);
      }

      NLogTrace("[Server] Final navigator after traversal: %p", (void *)navCurrent);
      if (navCurrent) {
        std::vector<std::string> parameters;
        if (httpIn.contains("parameters")) {
          parameters = httpIn["parameters"].get<std::vector<std::string>>();
        }

        if (parameters.empty()) {
          // NLogWarning("No parameter name provided for spectra !!!");
          // httpOut["result"] = "missing_parameters";
          // return;
          parameters =
              server->GetWorkspace()["spectra"]["properties"]["parameters"]["default"].get<std::vector<std::string>>();
        }
        else {
          // Update workspace default with provided parameters
          server->GetWorkspace()["spectra"]["properties"]["parameters"]["default"] = parameters;
          wsOut["workspace"]["spectra"]                                            = server->GetWorkspace()["spectra"];
        }
        NLogDebug("[Server] Parameters for PATCH spectra: %s", json(parameters).dump().c_str());

        double minmax = 0.05;
        if (httpIn.contains("axismargin")) {
          minmax = {httpIn["axismargin"].get<double>()};
        }
        std::string minmaxMode = "D";
        if (httpIn.contains("minmaxMode")) {
          minmaxMode = httpIn["minmaxMode"].get<std::string>();
        }

        std::string spectraPad = "pad3";
        if (httpIn.contains("startPad")) {
          spectraPad = httpIn["startPad"].get<std::string>();
        }

        // Parse starting pad index from pad name (e.g., pad4 → 4)
        int padIndex = 3;
        if (spectraPad.size() > 3 && spectraPad.substr(0, 3) == "pad") {
          try {
            padIndex = std::stoi(spectraPad.substr(3));
          }
          catch (...) {
            padIndex = 3;
          }
        }

        json spectraPayload;
        spectraPayload["point"] = point;
        // loop over all parameters and get spectra for each, then send as array to client
        std::vector<json> multiSpectra;
        for (const auto & param : parameters) {
          NLogDebug("[Server] Obtaining spectra for parameter '%s' at navigator level %d", param.c_str(),
                    navCurrent->GetLevel());
          std::string padName = "pad" + std::to_string(padIndex);
          TList *     spectra = navCurrent->DrawSpectraAll(param, {minmax}, minmaxMode, "");
          if (spectra) {
            NLogDebug("Spectra for parameter '%s' obtained:", param.c_str());
            json spectraObject = json::parse(TBufferJSON::ConvertToJSON(spectra).Data());
            // json debugAction;
            // debugAction["type"]                = "debug";
            // debugAction["message"]             = std::string("Debug click: ") + spectraObject["fName"].dump();
            // spectraObject["handlers"]["click"] = json::array({debugAction});
            spectraObject["targetPad"] = padName;
            spectraObject["parameter"] = param;
            multiSpectra.push_back(spectraObject);
          }
          else {
            NLogWarning("No spectra found for parameter '%s'", param.c_str());
          }
          padIndex++;
        }
        if (!multiSpectra.empty()) {
          wsOut["payload"]["spectra"]["objs"]       = multiSpectra;
          wsOut["payload"]["spectra"]["parameters"] = parameters;
          wsOut["payload"]["spectra"]["multipad"]   = true;
        }
        wsOut["payload"]["spectra"]["axismargin"] = minmax;
        if (!minmaxMode.empty()) {
          wsOut["payload"]["spectra"]["minmaxMode"] = minmaxMode;
        }

        httpOut["result"] = "success";
      }
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
        NLogTrace("Point navigator is available");
        // nav->Draw("hover");

        TH1 *   proj                   = nav->GetProjection();
        TString h                      = TBufferJSON::ConvertToJSON(proj);
        json    hMap                   = json::parse(h.Data());
        wsOut["payload"]["map"]["obj"] = hMap;
        json clickAction;
        clickAction["type"]                          = "http";
        clickAction["method"]                        = "GET";
        clickAction["contentType"]                   = "application/json";
        clickAction["path"]                          = "point";
        clickAction["payload"]                       = json::object();
        wsOut["payload"]["map"]["handlers"]["click"] = json::array({clickAction});

        // wsOut["map"]["handlers"]["hover"]["action"]       = "contenthover";
        // server->WebSocketBroadcast(wsOut);

        httpOut["result"] = "success";
      }
      else {
        NLogTrace("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else if (method.find("POST") != std::string::npos) {
      if (nav) {
        NLogTrace("Reshape navigator is available");
        // nav->Draw("hover");
        //
        int entry = httpIn.contains("entry") ? httpIn["entry"].get<int>() : -1;

        if (entry >= 0) {
          ngnt->GetEntry(entry);
          TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("outputPoint");
          if (outputPoint) {
            NLogTrace("Output point for bin %d:", entry);
            // outputPoint->ls();
            wsOut["payload"]["content"]["obj"]       = json::parse(TBufferJSON::ConvertToJSON(outputPoint).Data());
            wsOut["payload"]["content"]["targetPad"] = httpIn.contains("contentPad") ? httpIn["contentPad"] : "pad3";
          }
          else {
            NLogWarning("No output point found for entry %d", entry);
          }
        }

        httpOut["result"] = "success";
      }
      else {
        NLogTrace("Reshape navigator is not available");
        httpOut["result"] = "not_available";
        return;
      }
    }
    else {
      httpOut["error"] = "Unsupported HTTP method for reshape action";
    }
  };
}
