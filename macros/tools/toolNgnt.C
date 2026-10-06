///
/// toolNgnt.C — All-in-one tool macro registering the standard NGnTree actions
///              under the "ngnt" group prefix (served as HTTP API, WebSocket and MCP tools).
///
/// Registers: ngnt/open, ngnt/reshape, ngnt/map, ngnt/spectra, ngnt/point
///
/// URLs:  /api/ngnt/open, /api/ngnt/reshape, /api/ngnt/map, /api/ngnt/spectra, /api/ngnt/point
///
/// The five actions form one pipeline, and say so through the MCP metadata: each action
/// declares what has to have run before it (NMcpToolInfo::dependsOn), so `tools/list`
/// reads in the order the tools are used and a call out of turn is refused naming the
/// action to run first. `spectra` and `point` both branch off `map`; their NMcpToolInfo::order
/// sequences them (spectra before point). Each action also names a node in the combination tree
/// through NMcpToolInfo::label, a template filled from its own arguments (`{{ file }}`,
/// `{{ binningName }} ({{ levels }})`), so the tree reads as the analysis rather than as the action.
///
///   open ──▶ reshape ──▶ map ──┬─▶ spectra
///                              └─▶ point
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
#include <ndmspc/http/NInstanceTree.h>
#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/core/NCancellation.h>
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
  const bool       hasBin   = httpIn.contains("args") && httpIn["args"].is_object() && httpIn["args"].contains("bin");

  const int level = ctx.GetInt("level");
  if (level >= 0 && point.size() > static_cast<size_t>(level) && (!hasPoint || hasBin)) {
    point.resize(level);
  }
  if (hasBin) {
    point.push_back(httpIn["args"]["bin"].get<int>());
  }
  return point;
}

// The pad an action should use.
//
// A form sends the pad it was filled in with; a *click* does not — the action the tool attached to an
// object carries only the point/entry — so a drill would otherwise always land back on the default pad
// instead of the one the user chose. The workspace default, which the step's POST recorded, is where
// that choice was kept.
std::string PadArg(Ndmspc::NRouteContext & ctx, const std::string & key, const std::string & fallback,
                   const std::string & route = "map")
{
  json & in = ctx.In();
  if (in.contains(key) && in[key].is_string()) return in[key].get<std::string>();

  const json wsDef = ctx.GetWorkspaceDefault(route, key);
  if (wsDef.is_string() && !wsDef.get<std::string>().empty()) return wsDef.get<std::string>();
  return fallback;
}

// The combination node this request runs for, as the path a click on what it drew should name.
//
// A click carries no node of its own, so the router resolves its action against the group's live chain
// (NHttpServer::ActivePathFor) - correct only while that chain is still the one the drawing came from.
// Naming the node makes the click land on the map it was drawn for, wherever the live combination has
// moved since; without it a drawing left on the pad by another combination is refused with
// "no <action> node; run it first".
json DrawnNodePath(Ndmspc::NRouteContext & ctx)
{
  Ndmspc::NHttpServer * server = ctx.Server();
  if (server == nullptr) return json::array();
  const std::string node = server->GetCurrentInstance();
  if (node.empty()) return json::array();
  return json(Ndmspc::NInstanceTree(server->GetCombinations()).Path(node));
}

json BuildMapClickAction(const std::vector<int> & point, int level, const std::string & group = "",
                         const json & nodePath = json::array())
{
  json action;
  action["type"]             = "http";
  action["method"]           = "PATCH";
  action["path"]             = group.empty() ? "map" : group + "/map";
  action["contentType"]      = "application/json";
  action["payload"]          = json::object();
  action["payload"]["point"] = point;
  action["payload"]["level"] = level;
  // The map this click was drawn from (see DrawnNodePath): the drill belongs to that node, not to
  // whichever map the live combination is on by the time it is clicked.
  if (!nodePath.empty()) action["payload"]["path"] = nodePath;
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

/// A layer's tab name: the projection's axes as they are drawn, joined the way the spectra canvases
/// are (`phi-eta`). An axis with no title contributes nothing, and nothing at all falls back to the
/// histogram's own name.
std::string LayersLabel(TH1 * proj)
{
  std::string label;
  for (const auto * axis : {proj->GetXaxis(), proj->GetYaxis(), proj->GetZaxis()}) {
    if (axis == nullptr || axis->GetTitle() == nullptr || *axis->GetTitle() == '\0') continue;
    if (!label.empty()) label += '-';
    label += axis->GetTitle();
  }
  if (!label.empty()) return label;
  if (proj->GetName() != nullptr && *proj->GetName() != '\0') return proj->GetName();
  return "map";
}

/**
 * Draw every navigator layer from `nav` down, one envelope per layer, so the pad shows them as tabs.
 *
 * It is the walk `NGnNavigator::Draw` makes for its divided canvas — the node at its own level, then
 * the first child at each level below — here as one object per tab, each carrying that layer's own
 * click handlers.
 *
 * @param drill The path this drawing came from, so a click knows where it stands.
 * @return How many layers were drawn; none at all is a failure the caller reports.
 */
size_t RenderMapLayers(Ndmspc::NRouteContext & ctx, Ndmspc::NGnNavigator * nav, const std::string & pad,
                       const json & drill)
{
  const size_t           nLevels = nav->GetNLevels();
  Ndmspc::NGnNavigator * at      = nav;
  size_t                 drew    = 0;
  // The node these layers are drawn for, so a click on one drills this map rather than whichever map
  // the group's live combination is on when it is clicked.
  const json drawnAt = DrawnNodePath(ctx);
  for (size_t level = nav->GetLevel(); at != nullptr && level < nLevels; level++) {
    TH1 * proj = at->GetProjection();
    if (proj == nullptr) {
      NLogWarning("[Server] map: navigator level %zu has no projection, skipping that layer", level);
    }
    else {
      proj->SetStats(false);
      json clicks = json::array();
      clicks.push_back(BuildMapClickAction(drill, level, "ngnt", drawnAt));
      // One level above the last is where drilling stops, so a spectra makes sense there. That click
      // names no node on purpose: it targets a *spectra* node, and these layers' own node is a map one,
      // which the router would refuse ("node '…' is a ngnt/map, not a ngnt/spectra").
      if (level + 2 == nLevels) clicks.push_back(BuildSpectraClickAction(drill, level, "ngnt"));
      ctx.ShowRoot(proj, pad, LayersLabel(proj), "", json{{"click", clicks}});
      drew++;
    }
    if (at->GetChildren().empty()) break;
    at = at->GetChild(0);
  }
  return drew;
}

/// Show every parameter's spectra: one envelope per spectrum, on that parameter's pad, tabbed under
/// the object's own name (unique per spectrum, so asking again replaces its own tab).
bool RenderSpectra(Ndmspc::NRouteContext & ctx, Ndmspc::NGnNavigator * navCurrent,
                   const std::vector<std::string> & parameters, double axismargin, const std::string & minmaxMode,
                   int startPadIndex, bool addDebugAction = false)
{
  int  padIndex = startPadIndex;
  bool drew     = false;

  for (const auto & param : parameters) {
    NLogTrace("[Server] Obtaining spectra for parameter '%s' at navigator level %d", param.c_str(),
              navCurrent->GetLevel());
    const std::string padName = "pad" + std::to_string(padIndex);

    std::unique_ptr<TList> spectra(navCurrent->DrawSpectraAll(param, {axismargin}, minmaxMode, ""));
    if (spectra) {
      NLogTrace("Spectra for parameter '%s' obtained:", param.c_str());

      json handlers = json::object();
      if (addDebugAction) {
        json debugAction;
        debugAction["type"]    = "debug";
        debugAction["message"] = "Debug click";
        handlers["click"]      = json::array({debugAction});
      }

      for (TObject * object : *spectra) {
        if (object == nullptr) continue;
        ctx.ShowRoot(object, padName, object->GetName(), "", handlers);
        drew = true;
      }

      spectra->SetOwner(kTRUE); // the list owns what it drew; going out of scope frees them
    }
    else {
      NLogWarning("No spectra found for parameter '%s'", param.c_str());
    }
    padIndex++;
  }

  return drew;
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
      .Title("Levels")
      .Description("A nested array of integers representing levels.")
      .Items("array")
      .ItemItems("integer")
      .Default(defaultLevels)
      .Select("binningName", binningNames)
      .Title("Binning")
      .Default(currentBinningName)
      .Build();
}

json BuildMapSchema(const std::string & mappingPad = "pad1", const std::string & contentPad = "pad2")
{
  return Ndmspc::NSchemaBuilder()
      .String("mappingPad")
      .Title("Mapping pad")
      .Default(mappingPad)
      .String("contentPad")
      .Title("Content pad")
      .Default(contentPad)
      .Boolean("averages")
      .Title("Average deeper levels")
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
      .Title("First pad")
      .Default(startPad)
      .MultiSelect("parameters", paramNames)
      .Title("Parameters")
      .Default(defaultParamsJson)
      .Select("minmaxMode", {"V", "VE", "D"})
      .Title("Minmax mode")
      .Default(minmaxMode)
      .Number("axismargin")
      .Title("Edge margin")
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
  Ndmspc::RegisterMcpTool(group + "/open",
                          {
                              .description =
                                  "Open or close an NGnTree ROOT file. POST with 'file' opens it, GET reports the "
                                  "currently opened file and its structure, DELETE closes it.",
                              .methods     = {"GET", "POST", "DELETE"},
                              .inputSchema = {{"properties",
                                               {{"file",
                                                 {{"type", "string"},
                                                  {"description", "Path to the NGnTree ROOT file to open (POST only)."},
                                                  {"default", "NSingleBinning01Gaus.root"}}}}}},
                              .order       = 1,
                              .label       = "{{ file }}",
                              .session     = true, // the opened file is part of the room's session
                          });
  Ndmspc::RegisterMcpTool(
      group + "/reshape",
      {
          .description = "Reshape the opened tree into a navigator. POST with 'binningName' and 'levels' "
                         "builds the navigator, GET returns its info, DELETE clears it.",
          .methods     = {"GET", "POST", "DELETE"},
          .inputSchema = {{"properties",
                           {{"binningName",
                             {{"type", "string"},
                              // One name out of the binnings that exist: the *list* of them is data, and
                              // arrives live in the workspace schema, which the merge keeps.
                              {"format", "select"},
                              {"description", "Binning definition name (optional)."}}},
                            {"levels",
                             {{"type", "array"},
                              {"description", "Nested levels array, e.g. [[0,1,2],[3,4]]."},
                              {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}}}}}}}},
          .dependsOn   = {group + "/open"},
          .order       = 2,
          .label       = "{{ binningName }} ({{ levels }})",
          .session     = true, // the navigator the reshape builds is part of the session
      });
  Ndmspc::RegisterMcpTool(
      group + "/map",
      {
          .description = "Project the current navigator level onto pads. POST renders the projection "
                         "(mappingPad, contentPad, averages), PATCH drills down using a point/level/bin, "
                         "DELETE clears the map.",
          .methods     = {"POST", "PATCH", "DELETE"},
          .inputSchema = {{"properties",
                           {{"mappingPad", {{"type", "string"}, {"description", "Pad that shows the map."}}},
                            {"contentPad", {{"type", "string"}, {"description", "Pad that shows the content."}}},
                            {"averages",
                             {{"type", "boolean"}, {"description", "Average deeper levels into higher levels."}}},
                            {"point",
                             {{"type", "array"},
                              {"items", {{"type", "integer"}}},
                              // Drilling into the mapping is what sets it, so the form does not ask for it.
                              {"hidden", true},
                              {"description", "Canonical drill-down point: full path of child indices to select."}}},
                            {"level",
                             {{"type", "integer"},
                              // Which level was clicked is the drill's to say too, so the form does not ask.
                              {"hidden", true},
                              {"description", "Target navigator level; used with the stored state point when 'point' "
                                              "is omitted."}}}}}},
          .dependsOn   = {group + "/reshape"},
          .order       = 3,
          .label       = "{{ mappingPad }}",
      });
  Ndmspc::RegisterMcpTool(
      group + "/spectra",
      {
          .description = "Render spectra histograms for selected parameters (POST/PATCH) with 'parameters', "
                         "'startPad', 'axismargin' and 'minmaxMode'.",
          .methods     = {"POST", "PATCH", "DELETE"},
          .inputSchema = {{"properties",
                           {{"parameters",
                             {{"type", "array"},
                              {"format", "multiselect"},
                              // The parameters that are in the tree are data, and arrive live in the
                              // workspace schema, which the merge keeps.
                              {"items", {{"type", "string"}}},
                              {"description", "Parameters to draw spectra for (0 = all)."}}},
                            {"startPad", {{"type", "string"}, {"description", "First pad index, e.g. 'pad3'."}}},
                            {"axismargin", {{"type", "number"}}},
                            {"minmaxMode", {{"type", "string"}, {"format", "select"}, {"enum", {"V", "VE", "D"}}}},
                            {"point",
                             {{"type", "array"},
                              {"items", {{"type", "integer"}}},
                              // Drilling into the spectra is what sets it, so the form does not ask for it.
                              {"hidden", true},
                              {"description", "Canonical drill-down point: full path of child indices to select."}}},
                            {"level",
                             {{"type", "integer"},
                              // Which level was clicked is the drill's to say here too.
                              {"hidden", true},
                              {"description", "Target navigator level; used with the stored state point when 'point' "
                                              "is omitted."}}}}}},
          .dependsOn   = {group + "/map"},
          .order       = 4,
          .label       = "{{ parameters }}",
      });
  Ndmspc::RegisterMcpTool(
      group + "/point",
      {
          .description = "Fetch entry-level data points. GET returns the projection, POST with 'entry' and "
                         "'contentPad' returns the entry content.",
          .methods     = {"GET", "POST"},
          .inputSchema = {{"properties",
                           {{"entry", {{"type", "integer"}, {"description", "Entry index to fetch (POST)."}}},
                            {"contentPad", {{"type", "string"}}}}}},
          .dependsOn   = {group + "/map"},
          .order       = 5,
      });

  // ===========================================================================
  //  /api/ngnt/open — Open/close NGnTree files
  // ===========================================================================

  handlers[group + "/open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                 std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "ngnt";
    auto * server  = ctx.Server();
    auto * ngnt    = ctx.GetObject<Ndmspc::NGnTree>(ctx.ObjectName("ngnt"));

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
        server->RemoveInputObject(ctx.ObjectName("ngnt"));
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

      server->AddInputObject(ctx.ObjectName("ngnt"), ngnt);
      return;
    }

    if (ctx.IsDelete()) {
      if (ngnt) {
        NLogTrace("Closing NGnTree %s", ngnt->GetStorageTree()->GetFileName().c_str());
        ngnt->Close(false);
        server->RemoveInputObject(ctx.ObjectName("ngnt"));
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
    auto * ngnt    = ctx.GetObject<Ndmspc::NGnTree>(ctx.ObjectName("ngnt"));
    if (!ngnt || ngnt->IsZombie()) {
      NLogError("NGnTree is not opened, cannot reshape");
      ctx.Result("File %s not opened", ngnt ? ngnt->GetStorageTree()->GetFileName().c_str() : "unknown");
      return;
    }

    auto * nav = ctx.GetObject<Ndmspc::NGnNavigator>(ctx.ObjectName("navigator"));

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
        server->RemoveInputObject(ctx.ObjectName("navigator"));
        nav = nullptr;
      }

      nav = ngnt->Reshape(binningName, levels, 0, {}, {});
      if (Ndmspc::NCancellation::IsCancelled()) {
        // The reshape was cancelled mid-way: the navigator is partial and belongs to no session, so
        // drop it and report a cancellation (the client stops waiting without an error notice).
        NLogInfo("[CANCEL][reshape] cancelled; discarding partial navigator %p", (void *)nav);
        delete nav;
        httpOut["result"] = "failure";
        httpOut["code"]   = "cancelled";
        httpOut["error"]  = "Cancelled";
        return;
      }
      if (!nav) {
        ctx.Result("Failed to reshape NGnTree with provided levels [" + json(levels).dump() + "] and binning '" +
                   binningName + "'");
        return;
      }

      server->AddInputObject(ctx.ObjectName("navigator"), nav);

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
        server->RemoveInputObject(ctx.ObjectName("navigator"));
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
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>(ctx.ObjectName("ngnt"));
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>(ctx.ObjectName("navigator"));
    if (!nav) return;

    std::string mapKey     = "map";
    std::string spectraKey = "spectra";

    if (ctx.IsPost()) {
      std::string mappingPad = ctx.GetString("mappingPad", "pad1");
      std::string contentPad = ctx.GetString("contentPad", "pad2");
      NLogTrace("Mapping pad: %s, Content pad: %s", mappingPad.c_str(), contentPad.c_str());

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
        // Dump to file for debugging
        const char * tmpFile = gSystem->Getenv("NDMSPC_NGNG_EXPORT_JSON_FILE");
        if (tmpFile) {
          Ndmspc::NUtils::SaveRawFile(tmpFile, nested.dump());
          NLogDebug("[Server] Exported nested navigator structure to %s", tmpFile);
        }
      }

      // One envelope per layer, so the pad tabs them: the projection at this level and then the first
      // child's at each level below. The object goes as the projection itself, never as a list, which
      // jsroot would draw into one canvas, item on top of item.
      if (RenderMapLayers(ctx, nav, mappingPad, json::array()) == 0) {
        NLogError("[Server] map POST: no projection to show for nav=%p", (void *)nav);
        ctx.Result("Failed to get projection for the current navigator level " + std::to_string(nav->GetLevel()) +
                   ", cannot render map");
        return;
      }

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
        // The layers from where the drill landed down, replacing what the pad had: the tab strip
        // describes the current position rather than growing a trail.
        const std::string mappingPad = PadArg(ctx, "mappingPad", "pad1");
        RenderMapLayers(ctx, navCurrent, mappingPad, point);

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
        // A click names the cell it landed in, not a tree entry: jsroot reports the cell's content as
        // `args.cont`, and the ngnt navigator stores entry + 1 in its cells (NGnNavigator::Reshape),
        // so a click's entry is one less than that. The translation belongs here, in the tool that
        // knows what its cells mean — the UI only passes the click's args on.
        int entry = ctx.GetInt("entry");
        if (entry < 0 && httpIn.contains("args") && httpIn["args"].is_object() && httpIn["args"].contains("cont") &&
            httpIn["args"]["cont"].is_number()) {
          entry = httpIn["args"]["cont"].get<int>() - 1;
        }
        if (entry >= 0) {
          ngnt->GetEntry(entry);
          TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("_outputPoint");
          if (outputPoint) {
            NLogTrace("Output point for entry %d:", entry);
            const std::string pad = PadArg(ctx, "contentPad", "pad2");
            // One envelope per object, named after itself, so clicking the same cell again replaces
            // its own tab rather than piling up.
            for (TObject * object : *outputPoint) {
              if (object != nullptr) ctx.ShowRoot(object, pad, object->GetName());
            }
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
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>(ctx.ObjectName("ngnt"));
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>(ctx.ObjectName("navigator"));
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
      RenderSpectra(ctx, navCurrent, parameters, minmax, minmaxMode, padIndex, true);

      ctx.Success();
      return;
    }

    if (ctx.IsPatch()) {
      NLogTrace("[Server] PATCH spectra received: %s", httpIn.dump().c_str());

      const bool hasPoint = httpIn.contains("point") && httpIn["point"].is_array();
      const bool hasBin   = httpIn.contains("args") && httpIn["args"].is_object() && httpIn["args"].contains("bin");
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

      RenderSpectra(ctx, navCurrent, parameters, minmax, minmaxMode, padIndex);

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
    auto * ngnt    = ctx.RequireObject<Ndmspc::NGnTree>(ctx.ObjectName("ngnt"));
    if (!ngnt || ngnt->IsZombie()) return;
    auto * nav = ctx.RequireObject<Ndmspc::NGnNavigator>(ctx.ObjectName("navigator"));
    if (!nav) return;

    if (ctx.IsGet()) {
      TH1 * proj = nav->GetProjection();
      proj->SetStats(false);

      json clickAction;
      clickAction["type"]        = "http";
      clickAction["method"]      = "GET";
      clickAction["contentType"] = "application/json";
      clickAction["path"]        = "ngnt/point";
      clickAction["payload"]     = json::object();

      // A point's projection goes to the first pad, the way the map does.
      ctx.ShowRoot(proj, "pad1", "map", "", json{{"click", json::array({clickAction})}});

      ctx.Success();
      return;
    }

    if (ctx.IsPost()) {
      int entry = ctx.GetInt("entry");
      if (entry >= 0) {
        ngnt->GetEntry(entry);
        TList * outputPoint = (TList *)ngnt->GetStorageTree()->GetBranchObject("_outputPoint");
        if (outputPoint) {
          NLogTrace("Output point for entry %d:", entry);
          const std::string pad = PadArg(ctx, "contentPad", "pad2");
          for (TObject * object : *outputPoint) {
            if (object != nullptr) ctx.ShowRoot(object, pad, object->GetName());
          }
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
