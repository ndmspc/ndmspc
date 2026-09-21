///
/// toolNgnt.C — All-in-one tool macro registering the standard NGnTree actions
///              under the "ngnt" group prefix (served as HTTP API, WebSocket and MCP tools).
///
/// Registers: ngnt/open, ngnt/reshape, ngnt/map, ngnt/spectra, ngnt/point
///
/// URLs:  /api/ngnt/open, /api/ngnt/reshape, /api/ngnt/map, /api/ngnt/spectra, /api/ngnt/point
///
/// Usage:
///   ndmspc-server -m "toolNgnt.C"
///
/// To add custom tools alongside the built-in ones, create your own macro:
///
///   #include <ndmspc/http/NRouteContext.h>
///   #include <ndmspc/http/NSchemaBuilder.h>
///   #include <ndmspc/http/NHttpServer.h>
///
///   void toolMyCustom() {
///     auto & handlers = *(Ndmspc::gNdmspcHttpHandlers);
///
///     // Describe the action for MCP clients (ndmspc-mcp / POST /api/mcp).
///     Ndmspc::RegisterMcpTool("myplugin/summary", "Return a summary of the current state.");
///
///     handlers["myplugin/summary"] = [](std::string method, json & in, json & out, json & wsOut,
///                              std::map<std::string, TObject *> & objects) {
///       Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
///       if (ctx.IsGet()) {
///         out["info"] = "Custom summary endpoint";
///         ctx.Success();
///       }
///     };
///   }
///
/// Then load: ndmspc-server -m "toolNgnt.C,toolMyCustom.C"
///
/// RegisterMcpTool accepts a full Ndmspc::NMcpToolInfo, e.g.
///   Ndmspc::RegisterMcpTool("myplugin/summary", {
///       .description = "Return a summary of the current state.",
///       .title       = "Summary",
///       .methods     = {"GET"},
///       .hidden      = false,
///       .inputSchema = {{"properties", {{"verbose", {{"type", "boolean"}}}}}},
///   });
/// Call it right before (or after) the corresponding handlers[...] assignment.
///

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <TBufferJSON.h>
#include <TH1.h>
#include <TList.h>
#include <TString.h>
#include <TSystem.h>

#include <ndmspc/http/NRouteContext.h>
#include <ndmspc/http/NSchemaBuilder.h>
#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NGnNavigator.h>
#include <ndmspc/core/NParameters.h>
#include <ndmspc/core/NUtils.h>


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

// Resolves the drill-down point for a map/spectra PATCH request.
//
// Programmatic clients (MCP) send the full path in 'point'; when it is omitted,
// 'level' truncates the stored state point. Histogram clicks instead send the
// *base* point for the clicked level plus the clicked cell as 'args.bin', so the
// bin has to be appended to complete the path.
std::vector<int> ResolveDrillPoint(const Ndmspc::NRouteContext & ctx, const json & httpIn)
{
  const bool       hasPoint = httpIn.contains("point") && httpIn["point"].is_array();
  std::vector<int> point    = hasPoint ? httpIn["point"].get<std::vector<int>>() : ctx.GetStatePoint();
  const bool       hasBin =
      httpIn.contains("args") && httpIn["args"].is_object() && httpIn["args"].contains("bin");

  const int level = ctx.GetInt("level");
  if (level >= 0 && point.size() > static_cast<size_t>(level) && (!hasPoint || hasBin)) {
    point.resize(level);
  }
  if (hasBin) {
    point.push_back(httpIn["args"]["bin"].get<int>());
  }
  return point;
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
  std::string       objsArrayJson = "[";  // Build array as raw JSON string
  bool              first = true;

  for (const auto & param : parameters) {
    NLogTrace("[Server] Obtaining spectra for parameter '%s' at navigator level %d", param.c_str(),
              navCurrent->GetLevel());
    std::string padName = "pad" + std::to_string(padIndex);
    TList *     spectra = navCurrent->DrawSpectraAll(param, {axismargin}, minmaxMode, "");
    if (spectra) {
      NLogTrace("Spectra for parameter '%s' obtained:", param.c_str());
      std::string rawJson = TBufferJSON::ConvertToJSON(spectra).Data();
      spectra->SetOwner(kTRUE);
      delete spectra;

      // Build metadata to merge with raw JSON
      json metadata;
      metadata["targetPad"] = padName;
      metadata["parameter"] = param;
      
      if (addDebugAction) {
        json debugAction;
        debugAction["type"]           = "debug";
        debugAction["message"]        = "Debug click";
        metadata["handlers"]["click"] = json::array({debugAction});
      }

      // Merge raw JSON with metadata (returns string)
      std::string merged = Ndmspc::NUtils::MergeRawJsonWithMetadata(rawJson, metadata);
      
      // Append to array string
      if (!first) objsArrayJson += ",";
      objsArrayJson += merged;
      first = false;
    }
    else {
      NLogWarning("No spectra found for parameter '%s'", param.c_str());
    }
    padIndex++;
  }
  
  objsArrayJson += "]";

  if (!objsArrayJson.empty() && objsArrayJson != "[]") {
    // Inject the entire array as raw JSON
    Ndmspc::NUtils::AddRawJsonInjection(wsOut, {"payload", "spectra", "objs"}, objsArrayJson);
    wsOut["payload"]["spectra"]["parameters"] = parameters;
    wsOut["payload"]["spectra"]["multipad"]   = true;
  }
  wsOut["payload"]["spectra"]["axismargin"] = axismargin;
  if (!minmaxMode.empty()) {
    wsOut["payload"]["spectra"]["minmaxMode"] = minmaxMode;
  }
  return !objsArrayJson.empty() && objsArrayJson != "[]";
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

  return Ndmspc::NSchemaBuilder()
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
  return Ndmspc::NSchemaBuilder()
      .String("mappingPad")
      .Default(mappingPad)
      .String("contentPad")
      .Default(contentPad)
      .Boolean("averages")
      .Description("Average deeper-level parameter values/errors into higher levels (disable for large navigators)")
      .Default(true)
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

  return Ndmspc::NSchemaBuilder()
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

void toolNgnt()
{
  auto &      handlers = *(Ndmspc::gNdmspcHttpHandlers);
  std::string group    = "ngnt";

  // ===========================================================================
  //  MCP tool metadata
  //  Surfaced by `ndmspc-mcp` (stdio) and POST /api/mcp (HTTP). Descriptions
  //  live here in the macro, not in C++, so they can be changed without
  //  recompiling the server. `methods` narrows the tool's `method` enum.
  // ===========================================================================
  Ndmspc::RegisterMcpTool(group + "/open", {
      .description = "Open or close an NGnTree ROOT file. POST with 'file' opens it, GET reports the "
                     "currently opened file and its structure, DELETE closes it.",
      .methods     = {"GET", "POST", "DELETE"},
      .inputSchema = {{"properties",
                       {{"file",
                         {{"type", "string"},
                          {"description", "Path to the NGnTree ROOT file to open (POST only)."}}}}}},
  });
  Ndmspc::RegisterMcpTool(group + "/reshape", {
      .description = "Reshape the opened tree into a navigator. POST with 'binningName' and 'levels' "
                     "builds the navigator, GET returns its info, DELETE clears it.",
      .methods     = {"GET", "POST", "DELETE"},
      .inputSchema = {{"properties",
                       {{"binningName",
                         {{"type", "string"}, {"description", "Binning definition name (optional)."}}},
                        {"levels",
                         {{"type", "array"},
                          {"description", "Nested levels array, e.g. [[0,1,2],[3,4]]."},
                          {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}}}}}}}},
  });
  Ndmspc::RegisterMcpTool(group + "/map", {
      .description = "Project the current navigator level onto pads. POST renders the projection "
                     "(mappingPad, contentPad, averages), PATCH drills down using a point/level/bin, "
                     "DELETE clears the map.",
      .methods     = {"POST", "PATCH", "DELETE"},
      .inputSchema = {{"properties",
                       {{"mappingPad", {{"type", "string"}, {"description", "Pad that shows the map."}}},
                        {"contentPad", {{"type", "string"}, {"description", "Pad that shows the content."}}},
                        {"averages", {{"type", "boolean"}, {"description", "Average deeper levels into higher levels."}}},
                        {"point",
                         {{"type", "array"},
                          {"items", {{"type", "integer"}}},
                          {"description", "Canonical drill-down point: full path of child indices to select."}}},
                        {"level",
                         {{"type", "integer"},
                          {"description", "Target navigator level; used with the stored state point when 'point' "
                                          "is omitted."}}}}}},
  });
  Ndmspc::RegisterMcpTool(group + "/spectra", {
      .description = "Render spectra histograms for selected parameters (POST/PATCH) with 'parameters', "
                     "'startPad', 'axismargin' and 'minmaxMode'.",
      .methods     = {"POST", "PATCH", "DELETE"},
      .inputSchema = {{"properties",
                       {{"parameters", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                        {"startPad", {{"type", "string"}, {"description", "First pad index, e.g. 'pad3'."}}},
                        {"axismargin", {{"type", "number"}}},
                        {"minmaxMode", {{"type", "string"}, {"enum", {"V", "VE", "D"}}}},
                        {"point",
                         {{"type", "array"},
                          {"items", {{"type", "integer"}}},
                          {"description", "Canonical drill-down point: full path of child indices to select."}}},
                        {"level",
                         {{"type", "integer"},
                          {"description", "Target navigator level; used with the stored state point when 'point' "
                                          "is omitted."}}}}}},
  });
  Ndmspc::RegisterMcpTool(group + "/point", {
      .description = "Fetch entry-level data points. GET returns the projection, POST with 'entry' and "
                     "'contentPad' returns the entry content.",
      .methods     = {"GET", "POST"},
      .inputSchema = {{"properties",
                       {{"entry", {{"type", "integer"}, {"description", "Entry index to fetch (POST)."}}},
                        {"contentPad", {{"type", "string"}}}}}},
  });

  // ===========================================================================
  //  /api/ngnt/open — Open/close NGnTree files
  // ===========================================================================

  handlers[group + "/open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                 std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
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
        ngnt->Close(false);
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

      if (!ctx.Workspace()[reshapeKey].contains("type")) {
        ctx.Workspace()[reshapeKey] = BuildReshapeSchema(ngnt);
      }
      wsOut["workspace"][reshapeKey] = ctx.Workspace()[reshapeKey];

      server->AddInputObject("ngnt", ngnt);
      return;
    }

    if (ctx.IsDelete()) {
      if (ngnt) {
        NLogTrace("Closing NGnTree %s", ngnt->GetStorageTree()->GetFileName().c_str());
        ngnt->Close(false);
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
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

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
        ctx.Result("Navigator not created for file %s; POST reshape first",
                   ngnt->GetStorageTree()->GetFileName().c_str());
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

      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "levels", levels);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "binningName", binningName);
      wsOut["workspace"][reshapeKey] = ctx.Workspace()[reshapeKey];

      if (!ctx.Workspace()[mapKey].contains("type")) {
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
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

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

      // Averaging can be disabled per request (or via the map workspace default) for large navigators
      bool averages = nav->GetAverageParameters();
      {
        json wsDef = ctx.GetWorkspaceDefault(mapKey, "averages");
        if (wsDef.is_boolean()) averages = wsDef.get<bool>();
      }
      if (httpIn.contains("averages") && httpIn["averages"].is_boolean()) averages = httpIn["averages"].get<bool>();

      if (nav->GetLevel() == 0) {
        json nested;
        json exportCfg;
        exportCfg["averages"] = averages;
        nav->ExportToJson(nested, nav, std::vector<std::string>{}, exportCfg);
        listJson["nested"] = nested;
        // Dump to file for debugging
        const char * tmpFile = gSystem->Getenv("NDMSPC_NGNG_EXPORT_JSON_FILE");
        if (tmpFile) {
          Ndmspc::NUtils::SaveRawFile( tmpFile,nested.dump());
          NLogDebug("[Server] Exported nested navigator structure to %s", tmpFile);
        }
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

      // A repeated map POST first drops the previous "map" entry, which also
      // drops it as an orphaned workspace key, so the schema has to be built
      // again here. Writing the defaults into a missing schema would otherwise
      // create stubs that carry only a default and lose their `type`.
      if (!ctx.Workspace()[mapKey].contains("type")) {
        ctx.Workspace()[mapKey] = BuildMapSchema(mappingPad, contentPad);
      }

      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[mapKey], "mappingPad", mappingPad);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[mapKey], "contentPad", contentPad);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[mapKey], "averages", averages);
      wsOut["workspace"][mapKey] = ctx.Workspace()[mapKey];

      if (!ctx.Workspace()[spectraKey].contains("type")) {
        ctx.Workspace()[spectraKey] = BuildSpectraSchema(ngnt);
      }
      wsOut["workspace"][spectraKey] = ctx.Workspace()[spectraKey];

      ctx.Success();
      return;
    }

    if (ctx.IsPatch()) {
      NLogTrace("[Server] PATCH map received: %s", httpIn.dump().c_str());

      std::vector<int> point = ResolveDrillPoint(ctx, httpIn);

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
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

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

      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "startPad", spectraPad);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "parameters", parameters);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "axismargin", minmax);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "minmaxMode", minmaxMode);

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

      const bool hasPoint = httpIn.contains("point") && httpIn["point"].is_array();
      const bool hasBin = httpIn.contains("args") && httpIn["args"].is_object() && httpIn["args"].contains("bin");
      if (!hasPoint && !hasBin && ctx.GetInt("level") < 0) {
        NLogTrace("[Server] PATCH spectra no level specified");
        ctx.Result("Missing level for PATCH spectra");
        return;
      }

      std::vector<int> point = ResolveDrillPoint(ctx, httpIn);
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
        // The spectra schema can be absent here (its entry may have been
        // dropped as an orphaned workspace key), so build it before setting a
        // default on it instead of creating a property stub without a `type`.
        if (!ctx.Workspace()[spectraKey].contains("type")) {
          ctx.Workspace()[spectraKey] = BuildSpectraSchema(ngnt, parameters);
        }
        Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[spectraKey], "parameters", parameters);
        wsOut["workspace"][spectraKey] = ctx.Workspace()[spectraKey];
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
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);

    wsOut["group"] = "ngnt";
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>("ngnt");
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>("navigator");
    if (!nav) return;

    if (ctx.IsGet()) {
      TH1 *   proj                   = nav->GetProjection();
      std::string h                  = TBufferJSON::ConvertToJSON(proj,3).Data();
      Ndmspc::NUtils::AddRawJsonInjection(wsOut, {"payload", "map", "obj"}, h);

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
