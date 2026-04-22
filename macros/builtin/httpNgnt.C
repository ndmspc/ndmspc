///
/// httpNgnt.C — All-in-one macro registering all standard NGnTree HTTP handlers
///              under the "ngnt" group prefix.
///
/// Registers: ngnt/open, ngnt/reshape, ngnt/map, ngnt/spectra, ngnt/point
///
/// URLs:  /api/ngnt/open, /api/ngnt/reshape, /api/ngnt/map, /api/ngnt/spectra, /api/ngnt/point
///
/// Usage:
///   ndmspc-server start ngnt -m "httpNgnt.C"
///
/// To add custom handlers alongside the built-in ones, create your own macro:
///
///   #include <NGnRouteContext.h>
///   #include <NGnSchemaBuilder.h>
///   #include <NGnHttpServer.h>
///
///   void httpMyCustom() {
///     auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);
///     handlers["myplugin/summary"] = [](std::string method, json & in, json & out, json & wsOut,
///                              std::map<std::string, TObject *> & objects) {
///       Ndmspc::NGnRouteContext ctx(method, in, out, wsOut, objects);
///       if (ctx.IsGet()) {
///         out["info"] = "Custom summary endpoint";
///         ctx.Success();
///       }
///     };
///   }
///
/// Then load: ndmspc-server start ngnt -m "httpNgnt.C,httpMyCustom.C"
///

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <NGnRouteContext.h>
#include <NGnSchemaBuilder.h>
#include <NGnHttpServer.h>
#include <NGnTree.h>
#include <NGnNavigator.h>
#include <NParameters.h>
#include <TBufferJSON.h>
#include <TH1.h>
#include <TList.h>
#include <TString.h>

// ============================================================================
//  Helper functions (formerly NGnHandlerUtils)
// ============================================================================

Ndmspc::NGnNavigator * TraverseNavigator(Ndmspc::NGnNavigator * root, const std::vector<int> & point)
{
  Ndmspc::NGnNavigator * nav = root;
  for (const auto & bin : point) {
    if (!nav) break;
    NLogTrace("[Server] Traversing to child navigator for bin: %d nav=%p", bin, (void *)nav);
    nav = nav->GetChild(bin);
  }
  return nav;
}

json BuildMapClickAction(const std::vector<int> & point, int level, const std::string & group = "")
{
  json action;
  action["type"]             = "http";
  action["method"]           = "PATCH";
  action["path"]             = group.empty() ? "map" : group + "/map";
  action["contentType"]      = "application/json";
  action["payload"]          = json::object();
  action["payload"]["point"] = point;
  action["payload"]["level"] = level;
  return action;
}

json BuildSpectraClickAction(const std::vector<int> & point, int level, const std::string & group = "")
{
  json action;
  action["type"]             = "http";
  action["method"]           = "PATCH";
  action["path"]             = group.empty() ? "spectra" : group + "/spectra";
  action["contentType"]      = "application/json";
  action["payload"]          = json::object();
  action["payload"]["point"] = point;
  action["payload"]["level"] = level;
  return action;
}

int ParsePadIndex(const std::string & padName, int defaultIndex = 3)
{
  if (padName.size() > 3 && padName.substr(0, 3) == "pad") {
    try {
      return std::stoi(padName.substr(3));
    }
    catch (...) {
    }
  }
  return defaultIndex;
}

bool RenderSpectra(Ndmspc::NGnNavigator * navCurrent, const std::vector<std::string> & parameters, double axismargin,
                   const std::string & minmaxMode, int startPadIndex, json & wsOut, bool addDebugAction = false)
{
  int               padIndex = startPadIndex;
  std::vector<json> multiSpectra;

  for (const auto & param : parameters) {
    NLogTrace("[Server] Obtaining spectra for parameter '%s' at navigator level %d", param.c_str(),
              navCurrent->GetLevel());
    std::string padName = "pad" + std::to_string(padIndex);
    TList *     spectra = navCurrent->DrawSpectraAll(param, {axismargin}, minmaxMode, "");
    if (spectra) {
      NLogTrace("Spectra for parameter '%s' obtained:", param.c_str());
      json spectraObject = json::parse(TBufferJSON::ConvertToJSON(spectra).Data());
      spectra->SetOwner(kTRUE);
      delete spectra;

      if (addDebugAction) {
        json debugAction;
        debugAction["type"]                = "debug";
        debugAction["message"]             = std::string("Debug click: ") + spectraObject["fName"].dump();
        spectraObject["handlers"]["click"] = json::array({debugAction});
      }
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
  wsOut["payload"]["spectra"]["axismargin"] = axismargin;
  if (!minmaxMode.empty()) {
    wsOut["payload"]["spectra"]["minmaxMode"] = minmaxMode;
  }
  return !multiSpectra.empty();
}

json BuildReshapeSchema(Ndmspc::NGnTree * ngnt)
{
  auto axes  = ngnt->GetBinning()->GetAxes();
  auto nAxes = axes.size();

  json      defaultLevels = json::array();
  const int MAX_PER_LEVEL = 3;
  for (size_t i = 0; i < nAxes; i += MAX_PER_LEVEL) {
    json level = json::array();
    for (int j = 0; j < MAX_PER_LEVEL && (i + j) < nAxes; j++) {
      level.push_back(static_cast<int>(i + j));
    }
    defaultLevels.push_back(level);
  }

  std::vector<std::string> binningNames       = ngnt->GetBinning()->GetDefinitionNames();
  std::string              currentBinningName = ngnt->GetBinning()->GetCurrentDefinitionName();
  if (currentBinningName.empty() && !binningNames.empty()) {
    currentBinningName = binningNames.front();
  }
  else if (!binningNames.empty() &&
           std::find(binningNames.begin(), binningNames.end(), currentBinningName) == binningNames.end()) {
    currentBinningName = binningNames.front();
  }

  std::string hint;
  int         idx = 0;
  for (auto axis : axes) {
    hint += TString::Format("[%d] %s: %d bins \n", idx++, axis->GetName(), axis->GetNbins()).Data();
  }

  return Ndmspc::NGnSchemaBuilder()
      .Hint(hint)
      .Array("levels")
      .Description("A nested array of integers representing levels.")
      .Items("array")
      .ItemItems("integer")
      .Default(defaultLevels)
      .Select("binningName", binningNames)
      .Default(currentBinningName)
      .Build();
}

json BuildMapSchema(const std::string & mappingPad = "pad1", const std::string & contentPad = "pad2")
{
  return Ndmspc::NGnSchemaBuilder()
      .String("mappingPad")
      .Default(mappingPad)
      .String("contentPad")
      .Default(contentPad)
      .Build();
}

json BuildSpectraSchema(Ndmspc::NGnTree * ngnt, const std::vector<std::string> & defaultParams = {},
                        const std::string & startPad = "pad3", double axismargin = 1.0,
                        const std::string & minmaxMode = "V")
{
  Ndmspc::NParameters *    nparams    = ngnt->GetParameters();
  std::vector<std::string> paramNames = nparams ? nparams->GetNames() : std::vector<std::string>{};

  json defaultParamsJson;
  if (!defaultParams.empty()) {
    defaultParamsJson = defaultParams;
  }
  else if (!paramNames.empty()) {
    defaultParamsJson = json::array({paramNames.front()});
  }
  else {
    defaultParamsJson = json::array();
  }

  return Ndmspc::NGnSchemaBuilder()
      .String("startPad")
      .Default(startPad)
      .MultiSelect("parameters", paramNames)
      .Default(defaultParamsJson)
      .Select("minmaxMode", {"V", "VE", "D"})
      .Default(minmaxMode)
      .Number("axismargin")
      .Default(axismargin)
      .Build();
}

// ============================================================================

void httpNgnt()
{
  auto &      handlers = *(Ndmspc::gNdmspcHttpHandlers);
  std::string group    = "ngnt";

  // ===========================================================================
  //  /api/ngnt/open — Open/close NGnTree files
  // ===========================================================================

  handlers[group + "/open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                 std::map<std::string, TObject *> & objects) {
    Ndmspc::NGnRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "ngnt";
    auto * server  = ctx.Server();
    auto * ngnt    = ctx.GetObject<Ndmspc::NGnTree>("ngnt");

    std::string openKey    = "open";
    std::string reshapeKey = "reshape";

    if (ctx.IsGet()) {
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
      }
      else {
        ctx.Result("File %s not opened", ngnt ? ngnt->GetStorageTree()->GetFileName().c_str() : "unknown");
      }
      return;
    }

    if (ctx.IsPost()) {
      if (!httpIn.contains("file")) {
        httpOut["error"] = "Missing 'file' parameter for open action";
        return;
      }

      std::string file = httpIn["file"].get<std::string>();
      NLogTrace("Opening NGnTree from file: %s", file.c_str());

      if (ngnt && !ngnt->IsZombie()) {
        if (file == ngnt->GetStorageTree()->GetFileName()) {
          NLogTrace("NGnTree already opened from file: %s", file.c_str());
          ctx.Success();
          return;
        }
        server->RemoveInputObject("ngnt");
      }

      ngnt = Ndmspc::NGnTree::Open(file);
      if (!ngnt) {
        NLogError("Failed to open NGnTree from file: %s", file.c_str());
        ctx.Result("Failed to open NGnTree from file: %s", file.c_str());
        return;
      }

      NLogTrace("Successfully opened NGnTree from file: %s", file.c_str());
      ctx.Success();

      ctx.Workspace()[openKey]["type"]                          = "object";
      ctx.Workspace()[openKey]["properties"]["file"]["type"]    = "string";
      ctx.Workspace()[openKey]["properties"]["file"]["default"] = file;
      wsOut["workspace"][openKey]                               = ctx.Workspace()[openKey];

      if (!ctx.Workspace().contains(reshapeKey)) {
        ctx.Workspace()[reshapeKey] = BuildReshapeSchema(ngnt);
      }
      wsOut["workspace"][reshapeKey] = ctx.Workspace()[reshapeKey];

      server->AddInputObject("ngnt", ngnt);
      return;
    }

    if (ctx.IsDelete()) {
      if (ngnt) {
        NLogTrace("Closing NGnTree %s", ngnt->GetStorageTree()->GetFileName().c_str());
        server->RemoveInputObject("ngnt");
      }
      ctx.Success();
      return;
    }

    httpOut["error"] = "Unsupported HTTP method for open action";
  };

  // ===========================================================================
  //  /api/ngnt/reshape — Reshape NGnTree into navigator
  // ===========================================================================

  handlers[group + "/reshape"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                    std::map<std::string, TObject *> & objects) {
    Ndmspc::NGnRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

    wsOut["group"] = "ngnt";
    auto * server  = ctx.Server();
    auto * ngnt    = ctx.GetObject<Ndmspc::NGnTree>("ngnt");
    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened, cannot reshape");
      ctx.Result("File %s not opened", ngnt ? ngnt->GetStorageTree()->GetFileName().c_str() : "unknown");
      return;
    }

    auto * nav = ctx.GetObject<Ndmspc::NGnNavigator>("navigator");

    std::string reshapeKey = "reshape";
    std::string mapKey     = "map";

    if (ctx.IsGet()) {
      if (nav) {
        httpOut["info"] = nav->GetInfoJson();
        ctx.Success();
      }
      else {
        ctx.Result("File %s not opened", ngnt ? ngnt->GetStorageTree()->GetFileName().c_str() : "unknown");
      }
      return;
    }

    if (ctx.IsPost()) {
      std::string                   binningName = ctx.GetString("binningName");
      std::vector<std::vector<int>> levels      = ctx.GetParam("levels", std::vector<std::vector<int>>{});

      if (nav) {
        server->RemoveInputObject("navigator");
        nav = nullptr;
      }

      nav = ngnt->Reshape(binningName, levels, 0, {}, {});
      if (!nav) {
        ctx.Result("Failed to reshape NGnTree with provided levels [" + json(levels).dump() + "] and binning '" + binningName + "'");
        return;
      }

      server->AddInputObject("navigator", nav);

      if (!ctx.Workspace()[reshapeKey].contains("type")) {
        ctx.Workspace()[reshapeKey] = BuildReshapeSchema(ngnt);
      }

      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "levels", levels);
      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "binningName", binningName);
      wsOut["workspace"][reshapeKey] = ctx.Workspace()[reshapeKey];

      if (!ctx.Workspace().contains(mapKey)) {
        ctx.Workspace()[mapKey] = BuildMapSchema();
      }
      wsOut["workspace"][mapKey] = ctx.Workspace()[mapKey];

      ctx.Success();
      return;
    }

    if (ctx.IsDelete()) {
      NLogTrace("[DELETE][reshape] Closing reshape navigator %p", (void *)nav);
      if (nav) {
        server->RemoveInputObject("navigator");
      }
      ctx.Success();
      return;
    }

    httpOut["error"] = "Unsupported HTTP method for reshape action";
  };

  // ===========================================================================
  //  /api/ngnt/map — Project navigator histograms with drill-down
  // ===========================================================================

  handlers[group + "/map"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                std::map<std::string, TObject *> & objects) {
    Ndmspc::NGnRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

    wsOut["group"] = "ngnt";
    auto * server  = ctx.Server();
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>("ngnt");
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>("navigator");
    if (!nav) return;

    std::string mapKey     = "map";
    std::string spectraKey = "spectra";

    if (ctx.IsPost()) {
      std::string mappingPad = ctx.GetString("mappingPad", "pad1");
      std::string contentPad = ctx.GetString("contentPad", "pad2");
      NLogTrace("Mapping pad: %s, Content pad: %s", mappingPad.c_str(), contentPad.c_str());

      TList * l    = new TList();
      TH1 *   proj = nav->GetProjection();
      if (!proj) {
        NLogError("[Server] map POST: nav->GetProjection() returned nullptr for nav=%p", (void *)nav);
        ctx.Result("Failed to get projection for the current navigator level " + std::to_string(nav->GetLevel()) + ", cannot render map");
        delete l;
        return;
      }
      proj->SetStats(false);
      l->Add(proj);

      TString listStr  = TBufferJSON::ConvertToJSON(l);
      json    listJson = json::parse(listStr.Data());

      if (nav->GetLevel() == 0) {
        json nested;
        nav->ExportToJson(nested, nav, std::vector<std::string>{});
        listJson["nested"] = nested;
      }

      std::vector<int> pointForClickAction;
      for (auto & item : listJson["arr"]) {
        json clicks = json::array();
        clicks.push_back(BuildMapClickAction(json::array(), nav->GetLevel(), "ngnt"));

        json debugAction;
        debugAction["type"]    = "debug";
        debugAction["message"] = std::string("Debug click: ") + item["fName"].dump();
        clicks.push_back(debugAction);

        if (nav->GetLevel() == nav->GetNLevels() - 2) {
          clicks.push_back(BuildSpectraClickAction(pointForClickAction, nav->GetLevel(), "ngnt"));
        }
        item["handlers"]["click"] = clicks;
      }

      wsOut["payload"]["map"]["obj"]        = listJson;
      wsOut["payload"]["map"]["targetPad"]  = mappingPad;
      wsOut["payload"]["map"]["contentPad"] = contentPad;

      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[mapKey], "mappingPad", mappingPad);
      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[mapKey], "contentPad", contentPad);
      wsOut["workspace"][mapKey] = ctx.Workspace()[mapKey];

      if (!ctx.Workspace().contains(spectraKey)) {
        ctx.Workspace()[spectraKey] = BuildSpectraSchema(ngnt);
      }
      wsOut["workspace"][spectraKey] = ctx.Workspace()[spectraKey];

      ctx.Success();
      return;
    }

    if (ctx.IsPatch()) {
      NLogTrace("[Server] PATCH map received: %s", httpIn.dump().c_str());

      std::vector<int> point = ctx.GetStatePoint();

      int level = ctx.GetInt("level");
      if (level >= 0) {
        point.resize(level);
      }

      if (httpIn.contains("args")) {
        int bin = httpIn["args"]["bin"].get<int>();
        point.push_back(bin);
        NLogTrace("[Server] PATCH map added bin to point: %d", bin);
      }

      ctx.SetStatePoint(point);
      NLogTrace("[Server] Final point for PATCH map: %s", json(point).dump().c_str());

      Ndmspc::NGnNavigator * navCurrent = TraverseNavigator(nav, point);
      NLogTrace("[Server] Final navigator after traversal: %p", (void *)navCurrent);

      if (navCurrent && navCurrent->GetChildren().size() > 0) {
        TH1 * proj = navCurrent->GetProjection();
        proj->SetStats(false);
        TList l;
        l.Add(proj);
        TString listStr  = TBufferJSON::ConvertToJSON(&l);
        json    listJson = json::parse(listStr.Data());

        for (auto & item : listJson["arr"]) {
          json clicks = json::array();
          clicks.push_back(BuildMapClickAction(point, navCurrent->GetLevel(), "ngnt"));

          if (navCurrent->GetLevel() == nav->GetNLevels() - 2) {
            clicks.push_back(BuildSpectraClickAction(point, navCurrent->GetLevel(), "ngnt"));
          }
          item["handlers"]["click"] = clicks;
        }

        wsOut["payload"]["map"]["obj"]         = listJson;
        wsOut["payload"]["map"]["appendToTab"] = true;
        wsOut["payload"]["map"]["targetPad"]   = httpIn.contains("mappingPad") ? httpIn["mappingPad"] : "pad1";

        if (navCurrent->GetLevel() == nav->GetNLevels() - 1) {
          NLogTrace("[Server] Reached final level navigator for point: %s [%d/%d]", json(point).dump().c_str(),
                    navCurrent->GetLevel(), navCurrent->GetNLevels());

          auto & ws = ctx.Workspace();
          if (!ws.contains(spectraKey) || !ws[spectraKey].contains("properties") ||
              !ws[spectraKey]["properties"].contains("parameters") ||
              !ws[spectraKey]["properties"]["parameters"].contains("items") ||
              !ws[spectraKey]["properties"]["parameters"]["items"].contains("enum") ||
              ws[spectraKey]["properties"]["parameters"]["items"]["enum"].empty()) {

            if (!navCurrent->GetParameterNames().empty()) {
              auto names = navCurrent->GetParameterNames();
              NLogTrace("[Server] Updating spectra parameters: %s", json(names).dump().c_str());
              ws[spectraKey]["properties"]["parameters"]["items"]["enum"] = names;
              ws[spectraKey]["properties"]["parameters"]["default"] =
                  names.empty() ? json::array() : json::array({names.front()});
              wsOut["payload"]["workspace"][spectraKey] = ws[spectraKey];
            }
          }
        }
      }
      else {
        int entry = ctx.GetInt("entry");
        if (entry >= 0) {
          ngnt->GetEntry(entry);
          TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("_outputPoint");
          if (outputPoint) {
            NLogTrace("Output point for bin %d:", entry);
            std::string listStr                      = TBufferJSON::ConvertToJSON(outputPoint, 3).Data();
            // wsOut["payload"]["content"]       = nullptr;
            wsOut["payload"]["content"]["targetPad"] = httpIn.contains("contentPad") ? httpIn["contentPad"] : "pad2";
            Ndmspc::NUtils::AddRawJsonInjection(wsOut, {"payload", "content"}, listStr);
          }
          else {
            NLogTrace("No output point found for entry %d", entry);
            ctx.Result("No output point found for entry " + std::to_string(entry));
            return;
          }
        }
        else {
          NLogTrace("[Server] No entry and no projection found, nothing sent to websocket");
          ctx.Result("No entry and no projection found, nothing sent to websocket");
          return;
        }
      }

      ctx.Success();
      return;
    }

    if (ctx.IsDelete()) {
      NLogTrace("[DELETE][map] Cleaning up map state");
      ctx.Success();
      return;
    }

    httpOut["error"] = "Unsupported HTTP method for map action";
  };

  // ===========================================================================
  //  /api/ngnt/spectra — Render spectra histograms for selected parameters
  // ===========================================================================

  handlers[group + "/spectra"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                    std::map<std::string, TObject *> & objects) {
    Ndmspc::NGnRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

    wsOut["group"] = "ngnt";
    auto * server  = ctx.Server();
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>("ngnt");
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>("navigator");
    if (!nav) return;

    std::string spectraKey = "spectra";

    if (ctx.IsGet()) {
      return;
    }

    if (ctx.IsPost()) {

      std::string spectraPad = ctx.GetString("startPad", "pad3");

      std::vector<std::string> parameters = ctx.GetParam("parameters", std::vector<std::string>{});
      if (parameters.empty()) {
        json wsDef = ctx.GetWorkspaceDefault(spectraKey, "parameters");
        if (!wsDef.is_null() && wsDef.is_array()) {
          parameters = wsDef.get<std::vector<std::string>>();
        }
      }
      if (parameters.empty()) {
        Ndmspc::NParameters * nparams = ngnt->GetParameters();
        if (nparams) parameters = nparams->GetNames();
      }
      if (parameters.empty()) {
        NLogWarning("No parameter name provided for spectra !!!");
        ctx.Result("No parameter name provided for spectra");
        return;
      }

      double      minmax     = ctx.GetDouble("axismargin", 1.0);
      std::string minmaxMode = ctx.GetString("minmaxMode", "V");

      if (!ctx.Workspace()[spectraKey].contains("type")) {
        ctx.Workspace()[spectraKey] = BuildSpectraSchema(ngnt, parameters, spectraPad, minmax, minmaxMode);
      }

      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "startPad", spectraPad);
      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "parameters", parameters);
      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "axismargin", minmax);
      Ndmspc::NGnSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "minmaxMode", minmaxMode);

      std::vector<int>       point      = ctx.GetStatePoint();
      size_t                 nLevels    = nav->GetNLevels();
      Ndmspc::NGnNavigator * navCurrent = nav;

      for (size_t iLevel = 0; iLevel < nLevels - 1; iLevel++) {
        int bin = (iLevel < point.size()) ? point[iLevel] : -1;
        if (bin == -1) {
          NLogTrace("[Server] Point does not have bin for level %zu", iLevel);

          ctx.Result("Point does not have bin for level " + std::to_string(iLevel));
          return;
        }
        navCurrent = navCurrent->GetChild(bin);
        if (!navCurrent) break;
      }

      if (!navCurrent) {
        NLogTrace("No navigator found for spectra at point: %s", json(point).dump().c_str());

        ctx.Result("No navigator found for spectra at point: " + json(point).dump());
        return;
      }

      {
        auto & enumRef = ctx.Workspace()[spectraKey]["properties"]["parameters"]["items"]["enum"];
        if (enumRef.is_null() || enumRef.empty()) {
          Ndmspc::NParameters *    nparams    = ngnt->GetParameters();
          std::vector<std::string> paramNames = nparams ? nparams->GetNames() : navCurrent->GetParameterNames();

          enumRef = paramNames;
          if (ctx.Workspace()[spectraKey]["properties"]["parameters"]["default"].empty() && !paramNames.empty()) {
            ctx.Workspace()[spectraKey]["properties"]["parameters"]["default"] = json::array({paramNames.front()});
          }
        }
      }

      wsOut["workspace"][spectraKey] = ctx.Workspace()[spectraKey];

      int padIndex = ParsePadIndex(spectraPad);
      RenderSpectra(navCurrent, parameters, minmax, minmaxMode, padIndex, wsOut, true);

      ctx.Success();
      return;
    }

    if (ctx.IsPatch()) {
      NLogTrace("[Server] PATCH spectra received: %s", httpIn.dump().c_str());

      std::vector<int> point = ctx.GetStatePoint();

      int level = ctx.GetInt("level");
      if (level < 0) {
        NLogTrace("[Server] PATCH spectra no level specified");
        ctx.Result("Missing level for PATCH spectra");
        return;
      }
      if (point.size() > static_cast<size_t>(level)) {
        point.resize(level);
      }

      int newBin = -1;
      if (httpIn.contains("args")) {
        newBin = httpIn["args"]["bin"].get<int>();
      }
      point.push_back(newBin);
      NLogTrace("[Server] Final point for PATCH spectra: %s", json(point).dump().c_str());

      ctx.SetStatePoint(point);

      Ndmspc::NGnNavigator * navCurrent = TraverseNavigator(nav, point);
      NLogTrace("[Server] Final navigator after traversal: %p", (void *)navCurrent);
      if (!navCurrent) {
        ctx.Result("No navigator found for PATCH spectra at point: " + json(point).dump());
        return;
      }

      std::vector<std::string> parameters = ctx.GetParam("parameters", std::vector<std::string>{});
      if (parameters.empty()) {
        json wsDef = ctx.GetWorkspaceDefault(spectraKey, "parameters");
        if (!wsDef.is_null()) parameters = wsDef.get<std::vector<std::string>>();
      }
      else {
        ctx.Workspace()[spectraKey]["properties"]["parameters"]["default"] = parameters;
        wsOut["workspace"][spectraKey]                                     = ctx.Workspace()[spectraKey];
      }
      NLogTrace("[Server] Parameters for PATCH spectra: %s", json(parameters).dump().c_str());

      double minmax = 1.0;
      {
        json wsDef = ctx.GetWorkspaceDefault(spectraKey, "axismargin");
        if (!wsDef.is_null()) minmax = wsDef.get<double>();
      }
      if (httpIn.contains("axismargin")) minmax = httpIn["axismargin"].get<double>();

      std::string minmaxMode = "V";
      {
        json wsDef = ctx.GetWorkspaceDefault(spectraKey, "minmaxMode");
        if (!wsDef.is_null()) minmaxMode = wsDef.get<std::string>();
      }
      if (httpIn.contains("minmaxMode")) minmaxMode = httpIn["minmaxMode"].get<std::string>();

      std::string spectraPad = ctx.GetString("startPad", "pad3");
      int         padIndex   = ParsePadIndex(spectraPad);

      RenderSpectra(navCurrent, parameters, minmax, minmaxMode, padIndex, wsOut);

      ctx.Success();
      return;
    }

    if (ctx.IsDelete()) {
      NLogTrace("[DELETE][spectra] Cleaning up spectra state");
      ctx.Success();
      return;
    }

    httpOut["error"] = "Unsupported HTTP method for spectra action";
  };

  // ===========================================================================
  //  /api/ngnt/point — Retrieve entry-level data points
  // ===========================================================================

  handlers[group + "/point"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                  std::map<std::string, TObject *> & objects) {
    Ndmspc::NGnRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

    wsOut["group"] = "ngnt";
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>("ngnt");
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>("navigator");
    if (!nav) return;

    if (ctx.IsGet()) {
      TH1 *   proj                   = nav->GetProjection();
      TString h                      = TBufferJSON::ConvertToJSON(proj);
      json    hMap                   = json::parse(h.Data());
      wsOut["payload"]["map"]["obj"] = hMap;

      json clickAction;
      clickAction["type"]                          = "http";
      clickAction["method"]                        = "GET";
      clickAction["contentType"]                   = "application/json";
      clickAction["path"]                          = "ngnt/point";
      clickAction["payload"]                       = json::object();
      wsOut["payload"]["map"]["handlers"]["click"] = json::array({clickAction});

      ctx.Success();
      return;
    }

    if (ctx.IsPost()) {
      int entry = ctx.GetInt("entry");
      if (entry >= 0) {
        ngnt->GetEntry(entry);
        TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("_outputPoint");
        if (outputPoint) {
          NLogTrace("Output point for bin %d:", entry);
          TString outputPointStr                   = TBufferJSON::ConvertToJSON(outputPoint,3);
          // wsOut["payload"]["content"]       = nullptr;
          wsOut["payload"]["content"]["targetPad"] = httpIn.contains("contentPad") ? httpIn["contentPad"] : "pad2";
          Ndmspc::NUtils::AddRawJsonInjection(wsOut, {"payload", "content"}, outputPointStr.Data());
        }
        else {
          NLogWarning("No output point found for entry %d", entry);
        }
      }

      ctx.Success();
      return;
    }

    httpOut["error"] = "Unsupported HTTP method for point action";
  };
}
