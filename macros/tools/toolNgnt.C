///
/// toolNgnt.C — All-in-one tool macro registering the standard NGnTree actions
///              under the "ngnt" group prefix (served as HTTP API, WebSocket and MCP tools).
///
/// Registers: ndmspc/ngnt/open, ndmspc/ngnt/reshape, ndmspc/ngnt/map, ndmspc/ngnt/spectra, ndmspc/ngnt/point
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
#include <TCanvas.h>
#include <TH1.h>
#include <THStack.h>
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
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NParameters.h>
#include <ndmspc/core/NReshapeCache.h>
#include <ndmspc/core/NUtils.h>

// ============================================================================
//  Helper functions (formerly NGnHandlerUtils)
// ============================================================================

// Everything in this file except `toolNgnt()` lives in its own namespace. Tool macros are not compiled
// together: the server loads each one into the same Cling session, so two macros that each define a
// helper of the same name (`ScopedEnv`, `ScopedRunOutput`, …) collide. Cling answers a redefinition by
// trying to roll back the transaction and can crash in its unloader, so the failure reads as a
// segfault while loading the second macro rather than as the name clash it is. A namespace per tool
// keeps the helpers private, where an anonymous namespace does not: every unnamed namespace in a
// Cling session is the *same* namespace, which is what makes the two files collide in the first place.
namespace Ndmspc::NgntTools {

/**
 * Run output held on (or off) for as long as the object lives, and put back after.
 *
 * That is the flag `NUtils::ProgressBar` checks, so it is what makes a reshape's per-layer progress
 * visible in the server's log while the request runs — the logger reads `NDMSPC_LOG_RUN` once, at
 * startup, so no single request can turn it on through the environment.
 */
class ScopedRunOutput {
  public:
  explicit ScopedRunOutput(bool on) : fWas(Ndmspc::NLogger::GetRunOutput())
  {
    Ndmspc::NLogger::SetRunOutput(on);
  }
  ~ScopedRunOutput() { Ndmspc::NLogger::SetRunOutput(fWas); }
  ScopedRunOutput(const ScopedRunOutput &)            = delete;
  ScopedRunOutput & operator=(const ScopedRunOutput &) = delete;

  private:
  bool fWas{false};
};

/**
 * An environment variable set for as long as the object lives, and put back after.
 *
 * `NGnNavigator::Reshape` reads its worker count from `NDMSPC_NAV_WORKERS` at the start of every
 * reshape, so a request can set it for its own duration without touching the server's environment.
 */
class ScopedEnv {
  public:
  ScopedEnv(const std::string & name, const std::string & value) : fName(name)
  {
    const char * was = gSystem->Getenv(name.c_str());
    fWas             = was != nullptr ? was : "";
    fHad             = was != nullptr;
    gSystem->Setenv(fName.c_str(), value.c_str());
  }
  ~ScopedEnv()
  {
    if (fHad) gSystem->Setenv(fName.c_str(), fWas.c_str());
    else gSystem->Unsetenv(fName.c_str());
  }
  ScopedEnv(const ScopedEnv &)            = delete;
  ScopedEnv & operator=(const ScopedEnv &) = delete;

  private:
  std::string fName;
  std::string fWas;
  bool        fHad{false};
};

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

/**
 * Record the spectra call the macro's line makes, on the step itself.
 *
 * The step's arguments are the form's, and a form may leave `axismargin` or `minmaxMode` out — the room then
 * uses its own defaults, which the export cannot know (they are not the library's). So the room records what
 * it actually did: the parameter the line is for (the first of those drawn) and the two values it resolved.
 * It is the step's bookkeeping (`NInstanceTree::SetParams`), so its form never shows them, and the line the
 * tool declared reads the room's call rather than a guess at it.
 *
 * @param ctx The request.
 * @param parameters The parameters the spectra were drawn for (the first names the macro's line).
 * @param axismargin The edge margin in use.
 * @param minmaxMode The min/max mode in use.
 * @param point The layer it drew for, recorded as the step's own state (see `NRouteContext::SetStatePoint`).
 */
void RecordSpectraCall(Ndmspc::NRouteContext & ctx, const std::vector<std::string> & parameters, double axismargin,
                       const std::string & minmaxMode, const std::vector<int> & point)
{
  Ndmspc::NHttpServer * server = ctx.Server();
  if (server == nullptr) return;
  const std::string & node = server->GetCurrentInstance();
  if (node.empty()) return;

  Ndmspc::NInstanceTree tree(server->GetCombinations());
  if (!tree.Has(node)) return; // not a step of a combination: nothing to record on

  json params = tree.Params(node);
  if (!params.is_object()) params = json::object();
  params["parameter"]  = parameters.empty() ? std::string() : parameters.front();
  params["axismargin"] = axismargin;
  params["minmaxMode"] = minmaxMode;
  tree.SetParams(node, params);

  // The layer it is on, too: a step run while the room is drilled into one uses that layer (the live state the
  // drill mirrors), and the node has to hold it as its own or the macro would walk somewhere else - the state
  // is where a point lives (see NRouteContext::SetStatePoint, which the drill's own request does).
  tree.SetState(node, json{{"point", point}});
}

/**
 * Record the layer a step is on, when nothing has recorded one yet.
 *
 * A step's line reads `{{ state.point }}` to walk to the layer it is on, and a value the renderer does not
 * find is left out - which would write `GetNavigator()`, code that does not compile. So a step that makes
 * its object on a layer holds the layer it is on: **empty** when that layer is the navigator itself, which
 * is what a step run with no drill is on. An existing point is left alone: what the step is on is the layer
 * the user picked, and running the step again does not unpick it (only another drill moves it).
 *
 * @param ctx The request; the step it runs for is the room's current instance.
 */
void RecordLayerPoint(Ndmspc::NRouteContext & ctx)
{
  Ndmspc::NHttpServer * server = ctx.Server();
  if (server == nullptr) return;
  const std::string & node = server->GetCurrentInstance();
  if (node.empty()) return;

  Ndmspc::NInstanceTree tree(server->GetCombinations());
  if (!tree.Has(node)) return; // not a step of a combination: nothing to record on

  json recorded = tree.State(node);
  if (!recorded.is_object()) recorded = json::object();
  if (recorded.contains("point")) return;

  recorded["point"] = json::array();
  tree.SetState(node, recorded);
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

json BuildSpectraClickAction(const std::vector<int> & point, int level, const std::string & group = "",
                             const json & nodePath = json::array())
{
  json action;
  action["type"] = "http";
  // A **transient** POST: it draws the spectra for the clicked point and adds nothing to the
  // combination - the server creates no node for a `transient` POST (see NHttpServer::Dispatch), so a
  // click shows spectra on their pad and the tree stays as it was. A plain POST would leave a `spectra`
  // node behind on every click; a PATCH would target a node that does not exist yet and be refused
  // ("no ndmspc/ngnt/spectra node; run it first"). The node path names the map the click came from.
  action["method"]               = "POST";
  action["path"]                 = group.empty() ? "spectra" : group + "/spectra";
  action["contentType"]          = "application/json";
  action["payload"]              = json::object();
  action["payload"]["point"]     = point;
  action["payload"]["level"]     = level;
  action["payload"]["transient"] = true;
  if (!nodePath.empty()) action["payload"]["path"] = nodePath;
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

/// A layer's tab name: the **names** of the projection's axes, joined the way the spectra canvases are
/// (`phi-eta`). The name is what identifies an axis (`mean`) where its title is prose written for a
/// reader ("Mean [GeV]"), and a tab is an identifier - so it takes the name. Nothing at all falls back
/// to the histogram's own name.
///
/// Only the axes the projection actually has: `GetYaxis()` and `GetZaxis()` answer for a 1D or 2D
/// histogram too, carrying an empty title but a real name, so the dimension is what decides - taking
/// all three would name an axis that is not drawn.
std::string LayersLabel(TH1 * proj)
{
  std::vector<TAxis *> axes{proj->GetXaxis()};
  if (proj->GetDimension() >= 2) axes.push_back(proj->GetYaxis());
  if (proj->GetDimension() >= 3) axes.push_back(proj->GetZaxis());

  std::string label;
  for (const auto * axis : axes) {
    if (axis == nullptr) continue;
    const char * name = axis->GetName();
    if (name == nullptr || *name == '\0') continue;
    if (!label.empty()) label += '-';
    label += name;
  }
  if (!label.empty()) return label;
  if (proj->GetName() != nullptr && *proj->GetName() != '\0') return proj->GetName();
  return "map";
}

/// The slice a drill passed through, as one term per axis of each level above a layer. Defined below;
/// used by {@link RenderSpectra} for a spectrum's title.
std::string DrillLabel(Ndmspc::NGnNavigator * navCurrent, const std::vector<int> & point);

/**
 * Draw every navigator layer from `nav` down, one envelope per layer, so the pad shows them as tabs.
 *
 * It is the walk `NGnNavigator::Draw` makes for its divided canvas — the node at its own level, then
 * the first child at each level below — here as one object per tab, each carrying that layer's own
 * click handlers.
 *
 * @param drill The path this drawing came from, so a click knows where it stands.
 * @param replace Whether the first layer drops the pad's tabs (what a *fresh* map does, so a map for
 *        another combination does not linger beside it); a drill passes `false` and appends.
 * @return How many layers were drawn; none at all is a failure the caller reports.
 */
size_t RenderMapLayers(Ndmspc::NRouteContext & ctx, Ndmspc::NGnNavigator * nav, const std::string & pad,
                       const json & drill, bool replace, const json & nested = json())
{
  const size_t           nLevels = nav->GetNLevels();
  Ndmspc::NGnNavigator * at      = nav;
  size_t                 drew    = 0;
  // The bins this drawing sits under, one per level above a layer: the point the drill came in with,
  // then the first child taken at each level down - so every layer can say which slice it shows, the
  // same way a spectrum's title does.
  std::vector<int> walk    = drill.is_array() ? drill.get<std::vector<int>>() : std::vector<int>{};
  const json       drawnAt = DrawnNodePath(ctx);
  for (size_t level = nav->GetLevel(); at != nullptr && level < nLevels; level++) {
    TH1 * proj = at->GetProjection();
    if (proj == nullptr) {
      NLogWarning("[Server] map: navigator level %zu has no projection, skipping that layer", level);
    }
    else {
      proj->SetStats(false);
      // The histogram's title is the slice this layer is under - and nothing else: the layer's own name
      // is already its tab, and the projection's title named the ranges it was made for, which would say
      // the same a second time. A layer under nothing (the top of a fresh drawing) keeps its own title.
      const std::string slice = DrillLabel(at, walk);
      if (!slice.empty()) proj->SetTitle(slice.c_str());
      json clicks = json::array();
      clicks.push_back(BuildMapClickAction(drill, level, "ndmspc/ngnt", drawnAt));
      // One level above the last is where drilling stops, so a spectra makes sense there: the click also
      // draws the spectra for that point. That is a *transient* POST - it shows on the spectra pad and
      // adds nothing to the combination (see BuildSpectraClickAction).
      if (level + 2 == nLevels) clicks.push_back(BuildSpectraClickAction(drill, level, "ndmspc/ngnt", drawnAt));
      // The first layer carries `replace` for a fresh map, so the pad drops what it held (a map from
      // another combination included); a drill appends its layers to what is there.
      // The tab keeps the layer's own label: it is what identifies the tab, so anything that changes
      // with a drill would make a new tab instead of replacing the same one.
      // The first layer is the mapping histogram; it carries the navigator's nested export, as the
      // older `httpNgnt` map did on its list. The deeper layers are projections of the same data and
      // do not repeat it.
      const json extra =
          (drew == 0 && nested.is_object() && !nested.empty()) ? json{{"nested", nested}} : json::object();
      ctx.ShowRoot(proj, pad, LayersLabel(proj), "", json{{"click", clicks}}, replace && drew == 0, extra);
      drew++;
    }
    // The next layer down: the first child this level has. Not `GetChild(0)` - a bin is a ROOT bin
    // number (1-based), so index 0 is the underflow slot and is never a child; a navigator whose bin 1
    // holds no data has its first child further along, and this is the walk down every level.
    size_t                bin   = 0;
    Ndmspc::NGnNavigator * first = at->GetFirstChild(&bin);
    if (first == nullptr) break;
    // The bin this layer took is part of the next layer's slice.
    walk.push_back(static_cast<int>(bin));
    at = first;
  }
  return drew;
}

/// One axis of a slice. A categorical axis reads by its bin's label (`entries = 1000`); a numeric one
/// by the bin's range (`mean ∈ [2.25, 2.75]`) - a bin is an interval, not a point, so the centre would
/// hide its width and be wrong for a non-uniform axis. "" when the bin is outside the axis.
std::string AxisSlice(TAxis * axis, int bin)
{
  if (axis == nullptr || bin < 1 || bin > axis->GetNbins()) return "";
  // The axis's **name** (`mean`), not its title: a slice names the axis it is a slice of, and the
  // title is prose written for a reader ("Mean [GeV]") that reads oddly in the middle of one.
  const char *      axisName = axis->GetName();
  const std::string name =
      (axisName != nullptr && *axisName != '\0') ? axisName : std::string(axis->GetTitle());

  const char * label = axis->GetBinLabel(bin);
  if (label != nullptr && *label != '\0') return name + " = " + label;

  const auto edge = [](double value) { return std::string(TString::Format("%.3g", value).Data()); };
  return name + " ∈ [" + edge(axis->GetBinLowEdge(bin)) + ", " + edge(axis->GetBinUpEdge(bin)) + "]";
}

/// Takes a projection's global bin apart into one bin per axis: ROOT lays a `TH2`/`TH3` bin out as
/// `bx + by*(nx+2) + ...`, which is what `FindFixBin` returns - done here so no `TH2`/`TH3` header is
/// needed for the two numbers we want.
void GlobalToAxisBins(TH1 * proj, int global, int & bx, int & by, int & bz)
{
  const int nx = proj->GetNbinsX() + 2;
  const int ny = proj->GetNbinsY() + 2;
  bx            = global % nx;
  by            = (global / nx) % ny;
  bz            = global / (nx * ny);
}

/// The slice a drill passed through, as one term per axis of each level above - `Mean ∈ [2.25, 2.75]`
/// for a numeric axis, `Entries = 1000` for a categorical one - joined with `, `; "" when nothing was
/// drilled. It is appended to a spectrum's title, so a spectrum says which slice it was made in.
std::string DrillLabel(Ndmspc::NGnNavigator * navCurrent, const std::vector<int> & point)
{
  // The levels above the one the spectra are drawn at, root first: `navCurrent` is that level, so its
  // parents are the levels a drill passed through, and `point` names the cell taken at each.
  std::vector<Ndmspc::NGnNavigator *> chain;
  for (Ndmspc::NGnNavigator * up = navCurrent->GetParent(); up != nullptr; up = up->GetParent()) {
    chain.push_back(up);
  }
  std::reverse(chain.begin(), chain.end());

  std::vector<std::string> parts;
  for (size_t level = 0; level < chain.size() && level < point.size(); level++) {
    TH1 * proj = chain[level]->GetProjection();
    if (proj == nullptr || point[level] < 1) continue;
    int bx = 0;
    int by = 0;
    int bz = 0;
    GlobalToAxisBins(proj, point[level], bx, by, bz);
    const auto keep = [&parts](std::string part) {
      if (!part.empty()) parts.push_back(std::move(part));
    };
    keep(AxisSlice(proj->GetXaxis(), bx));
    if (proj->GetDimension() >= 2) keep(AxisSlice(proj->GetYaxis(), by));
    if (proj->GetDimension() >= 3) keep(AxisSlice(proj->GetZaxis(), bz));
  }

  std::string label;
  for (const auto & part : parts) {
    if (!label.empty()) label += ", ";
    label += part;
  }
  return label;
}

/// The object a pad shows a title from: the histogram or stack itself, or - for the canvas a spectrum
/// is handed back as - the first one inside it. Nothing else (an axis, a frame) carries the title.
TNamed * TitleBearer(TObject * object)
{
  if (object == nullptr) return nullptr;
  if (dynamic_cast<TH1 *>(object) != nullptr || dynamic_cast<THStack *>(object) != nullptr) {
    return dynamic_cast<TNamed *>(object);
  }
  if (auto * pad = dynamic_cast<TPad *>(object)) {
    if (TList * primitives = pad->GetListOfPrimitives()) {
      for (TObject * child : *primitives) {
        if (TNamed * found = TitleBearer(child)) return found;
      }
    }
  }
  return nullptr;
}

/// Show every parameter's spectra: one envelope per spectrum, on that parameter's pad, tabbed under
/// the object's own name (unique per spectrum, so asking again replaces its own tab).
bool RenderSpectra(Ndmspc::NRouteContext & ctx, Ndmspc::NGnNavigator * navCurrent,
                   const std::vector<std::string> & parameters, double axismargin, const std::string & minmaxMode,
                   int startPadIndex, const std::vector<int> & point, bool addDebugAction = false,
                   bool replace = false)
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

      // The slice the drill passed through rides on every spectrum's title, so a spectrum says which
      // point it belongs to ("meanFit : entries at Mean[-1.25]Sigma[2.0]"). A drawn spectrum is a
      // `TNamed` of one kind or another (a histogram, or the `THStack` a fit is drawn as), which is all
      // this needs to carry.
      const std::string slice      = DrillLabel(navCurrent, point);
      bool              firstOnPad = true;
      for (TObject * object : *spectra) {
        if (object == nullptr) continue;
        if (!slice.empty()) {
          TNamed * bearer = TitleBearer(object);
          if (bearer != nullptr) {
            const std::string suffix = " | " + slice;
            const auto        append = [&suffix](TNamed * named) {
              if (named == nullptr) return;
              const char * title = named->GetTitle();
              named->SetTitle(((title == nullptr ? std::string() : std::string(title)) + suffix).c_str());
            };
            append(bearer);
            // A stack draws its own histogram's title, so the suffix has to be on that too.
            if (auto * stack = dynamic_cast<THStack *>(bearer)) append(stack->GetHistogram());
          }
        }
        // The first spectrum on each pad carries `replace` for a fresh draw, so the pad drops what it
        // held before - spectra from another combination included, the same way a map's pad does. A
        // drill passes `replace=false` and appends to what is there.
        ctx.ShowRoot(object, padName, object->GetName(), "", handlers, replace && firstOnPad);
        firstOnPad = false;
        drew       = true;
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

  json        axisLabels = json::array();
  json        axisNames  = json::array();
  int         idx = 0;
  for (auto axis : axes) {
    axisLabels.push_back(TString::Format("[%d] %s: %d bins", idx, axis->GetName(), axis->GetNbins()).Data());
    axisNames.push_back(axis->GetName());
    idx++;
  }

  json schema = Ndmspc::NSchemaBuilder()
                    .Array("levels")
                    .Title("Levels")
                    .Description("A nested array of integers representing levels.")
                    .Items("array")
                    .ItemItems("integer")
                    .Default(defaultLevels)
                    .Select("binningName", binningNames)
                    .Title("Binning")
                    .Default(currentBinningName)
                    // How many threads reshape the first layer with. The core count by default, because a
                    // reshape reads every entry and is the slow step the worker count exists for.
                    // `AvailableCpuCount` rather than `hardware_concurrency`: inside a container the node's
                    // count is not what this process may use.
                    .Integer("nWorkers")
                    .Title("Workers")
                    .Description("Threads reshaping the first layer. Defaults to the CPUs this process may "
                                 "use; 1 keeps the reshape serial. Ignored by a single-level reshape.")
                    .Default(std::max(1, static_cast<int>(Ndmspc::NUtils::AvailableCpuCount())))
                    .Build();
  // What each level's numbers read as, so a level is built by picking an axis by name rather than by
  // counting them out (the levels editor's Multi / Names / IDs switch reads these) — and that a
  // navigator's axes are **unique**: each is one level's, all of them have to be represented, and the
  // editor says when they are.
  schema["properties"]["levels"]["axisLabels"] = axisLabels;
  schema["properties"]["levels"]["axisNames"]  = axisNames;
  schema["properties"]["levels"]["unique"]     = true;

  // What to do with the cache: whether it may be read and whether it may be written, as four answers. Short
  // enough to sit on one row, and each says what happens rather than naming a flag.
  schema["properties"]["cache"] = {
      {"type", "string"},
      {"title", "Mode"},
      {"format", "segmented"},
      {"enum", {"use", "use-or-store", "rebuild", "ignore"}},
      {"labels", {"Use", "Use & store", "Rebuild", "Ignore"}},
      {"default", "use-or-store"},
      {"description", "Use: load it when stored; on a miss reshape and keep nothing. Use & store: the same, "
                      "but the reshaped one is kept, so the cache fills itself (default). Rebuild: do not "
                      "load it; reshape and store over the old entry. Ignore: do not load it and keep "
                      "nothing — for iterating on a macro."}};

  // Where an entry is kept, defaulting to what the handler would pick for this kind of input: the input
  // file itself for local data, the cache directory for remote data.
  const std::string inputFile    = ngnt->GetStorageTree() ? ngnt->GetStorageTree()->GetFileName() : "";
  const std::string cacheHome    = Ndmspc::NReshapeCache::DefaultCacheFile(inputFile);
  const std::string cacheDirFile = Ndmspc::NReshapeCache::CacheFilePath(inputFile);

  // The two homes, offered as the paths they *are* rather than as "Input file" / "Cache directory": a user
  // choosing where an entry goes wants to see the file it lands in. Real paths also mean the dropdown has no
  // empty-string value, which a select would otherwise draw as "nothing chosen".
  std::string cacheShown = cacheDirFile;
  if (const char * dir = gSystem->Getenv("NDMSPC_CACHE_DIR"); dir != nullptr && dir[0] != '\0') {
    const std::string base = dir;
    if (cacheShown.rfind(base + "/", 0) == 0) cacheShown = "$NDMSPC_CACHE_DIR" + cacheShown.substr(base.size());
  }

  // `CacheFilePath` needs a usable cache directory; with none there is no separate home to offer, so the
  // input is the only choice.
  json homes = json::array();
  if (!cacheDirFile.empty() && cacheDirFile != inputFile) {
    homes.push_back(cacheDirFile);
  }
  if (!inputFile.empty()) homes.push_back(inputFile);

  json homeLabels = json::array();
  if (!cacheDirFile.empty() && cacheDirFile != inputFile) {
    homeLabels.push_back(cacheShown);
  }
  if (!inputFile.empty()) homeLabels.push_back(inputFile);

  // Defaulting to what the handler would pick for this kind of input: the input file for local data, the
  // cache file for remote data — which is never a path a select can show blank.
  std::string homeDefault = cacheHome.empty() ? cacheDirFile : cacheHome;
  if (homeDefault.empty()) homeDefault = inputFile;

  schema["properties"]["cacheFile"] = {
      {"type", "string"},
      {"title", "Store in"},
      {"format", "select"},
      {"enum", homes},
      {"labels", homeLabels},
      {"default", homeDefault},
      {"description", cacheHome.empty()
                          ? "Entries go to the cache file, which leaves the input untouched."
                          : "Entries go inside the input file, so the cache travels with the data."}};

  // What the form asks first is what a reshape is *about* — the binning and the combination — so those two
  // come first. `nWorkers` belongs to the reshape itself, not to where its result is kept, so it stays at the
  // root and sits directly above the section rather than inside it. A group is presentation only: the fields
  // keep their flat values, so a client sends `nWorkers`, `cache` and `cacheFile` as they are.
  schema["order"]  = {"binningName", "levels", "nWorkers", "cache", "cacheFile"};
  schema["groups"] = json::array({{{"title", "Cache"},
                                   {"fields", {"cache", "cacheFile"}},
                                   {"collapsed", false}}});
  return schema;
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

/// The NGNT workflow tour's steps, in order: the chain of actions a reader runs, one Save & Run at a
/// time. The spectra step (and its Save & Run) is only there when the tree has parameters to draw for:
/// a tree without them ends at the map, which is then the last step.
json NgntWorkflowTourSteps(const std::string & group, bool withSpectra)
{
  json steps = json::array({
      json{{"show", "group"}, {"body", "Choose the NGNT explorer."}},
      // No session lead-in card: running the first step is what starts the
      // session, and the tour says so at that step's own run card — one card
      // for it, not two.
      // The file is the room's own example, so the step is run as it stands.
      json{{"show", "stepBlock"},
           {"action", group + "/open"},
           {"body",
            "The first step of the chain: opening the file. What it "
            "takes sits below it, already filled in."}},
      json{{"show", "field:file"},
           {"body",
            "The file to open — the room's own example, "
            "`NSingleBinning01Gaus.root`. Leave it as it is: a "
            "single-binning cube with two parameters, mean and sigma."}},
      json{{"show", "saveAndRun"},
           {"body",
            "Run the step: it starts the session and reads the file, "
            "and the next step is added for it."}},
      // The navigator: the axes the form lists, the binning, the levels.
      json{{"show", "stepBlock"},
           {"action", group + "/reshape"},
           {"body",
            "A navigator is the tree cut into nested levels — what the "
            "analysis walks and what the pads draw."}},
      json{{"show", "stepForm"},
           {"body",
            "The list is the axes this file has, with the bins each "
            "one holds — `[0] mean: 5 bins`, `[1] sigma: 6 bins`."}},
      json{{"show", "field:binningName"},
           {"body",
            "Which **binning definition** to cut by. The file's own is "
            "already chosen."}},
      json{{"show", "field:levels"},
           {"body",
            "And how the axes are grouped into levels: each level is "
            "one cut the navigator can walk into. Three axes per level "
            "is fine — run it with these defaults."}},
      json{{"show", "saveAndRun"},
           {"body",
            "Run it: the navigator is built from the file, and "
            "everything below works on it."}},
      // The map: the two pads, then the run.
      json{{"show", "stepBlock"},
           {"action", group + "/map"},
           {"body",
            "Mapping draws the navigator's current level into a pad — "
            "one histogram per parameter, tiled."}},
      json{{"show", "field:mappingPad"}, {"body", "The pad the map itself is drawn in — `pad1`."}},
      json{{"show", "field:contentPad"},
           {"body",
            "And the pad a picked point's **content** goes to — "
            "`pad2`. It stays empty until something is picked."}},
      json{{"show", "saveAndRun"}, {"body", "Run it: the mapping appears in the pads, beside us."}},
      // The map is in pad1 now.
      json{{"show", "pad:pad1"},
           {"body",
            "The mapping, in `pad1`: one histogram per parameter of "
            "this level. Its bins are a live map — each one stands for "
            "a slice of the data below it."}},
      // A bin click drills: pad2 is created/redrawn. The whole pane, since the
      // pad is dynamic and appears beside the map.
      json{{"show", "pads"},
           {"body",
            "Click a bin of the mapping: what it stands for is drilled "
            "into and drawn beside it, in `pad2` — a pad that exists "
            "only once something has been picked. Click another bin and "
            "`pad2` is redrawn for that one; click deeper and it "
            "follows the level you are on."}},
  });

  // The spectra: the parameters (several at once) and the pad they start at. Only when the tree has
  // parameters — a tree without them ends the tour at the map, with no Save & Run after it.
  if (withSpectra) {
    steps.push_back(json{{"show", "stepBlock"},
                         {"action", group + "/spectra"},
                         {"body",
                          "The last step: a spectrum per parameter, drawn from the "
                          "place you are holding in the navigator."}});
    steps.push_back(json{{"show", "field:parameters"},
                         {"body",
                          "Which parameters to draw for — **several may be picked at "
                          "once**, and each gets a spectrum of its own. The first is "
                          "picked to begin with."}});
    steps.push_back(json{{"show", "field:startPad"},
                         {"body",
                          "The pad the spectra **start** at — `pad3`, the next free "
                          "one, so they land beside the mapping rather than over it. "
                          "One pad per parameter picked, onwards from here."}});
    steps.push_back(json{{"show", "saveAndRun"},
                         {"body",
                          "Run it: the spectra are added under the mapping, one "
                          "after another."}});
    steps.push_back(json{{"show", "pad:pad3"},
                         {"body",
                          "The spectra, from `pad3` on — one per parameter you "
                          "picked."}});
  }
  return steps;
}

/// The NGNT workflow tour: open, reshape, map, and — when the tree has parameters — spectra. With none,
/// the map is the last step and there is no Save & Run after it, because there is nothing to draw.
json NgntWorkflowTour(const std::string & group, bool withSpectra = true)
{
  std::string description =
      withSpectra
          ? "Open the room's example NGnTree, reshape it into a navigator, map that onto pads and draw "
            "spectra for its parameters — the analysis end to end."
          : "Open the room's example NGnTree, reshape it into a navigator and map that onto pads. This "
            "tree has no parameters, so the workflow ends at the map.";
  return json{{"name", "The NGNT workflow"},
              {"description", description},
              {"steps", NgntWorkflowTourSteps(group, withSpectra)}};
}

} // namespace Ndmspc::NgntTools

// ============================================================================

void toolNgnt()
{
  using namespace Ndmspc::NgntTools;

  auto &      handlers = *(Ndmspc::gNdmspcHttpHandlers);
  // The family's key prefix: `ndmspc` is the platform's namespace, `ngnt` the family (see the naming
  // rule in `http/README.md`), so the keys are `ndmspc/ngnt/open`, `ndmspc/ngnt/reshape`, …
  std::string group    = "ndmspc/ngnt";
  /** What the group is called where a user reads it (the room's group picker, Help). */
  const char * groupLabel = "NGNT explorer";

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
                                                  {"description",
                                                   "Path to the NGnTree ROOT file to open (POST only): a name in the "
                                                   "room's working directory, or an http(s) URL."},
                                                  {"default", "NSingleBinning01Gaus.root"}}}}}},
                              .order       = 1,
                              .label       = "{{ file }}",
                              // What this step is in C++, for a combination written out as a macro
                              // (ndmspc/macro): the file it opened, opened the same way here — the object the
                              // user goes on with in ROOT, not a drawing of it.
                              .macroLines    = {"auto * ngnt = Ndmspc::NGnTree::Open({{ file }});"},
                              .macroIncludes = {"ndmspc/core/NGnTree.h"},
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
                              {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}}}}},
                            {"nWorkers",
                             {{"type", "integer"},
                              {"default", std::max(1, static_cast<int>(Ndmspc::NUtils::AvailableCpuCount()))},
                              {"description", "Threads reshaping the first layer. Defaults to the CPUs this "
                                              "process may use; 1 keeps the reshape serial. Ignored by a "
                                              "single-level reshape."}}},
                            {"cache",
                             {{"type", "string"},
                              {"format", "segmented"},
                              {"enum", {"use", "use-or-store", "rebuild", "ignore"}},
                              {"labels", {"Use", "Use & store", "Rebuild", "Ignore"}},
                              // Declared here as well as in the workspace schema: the form is built from
                              // this copy until the tool has published its own, and a bare string would be
                              // drawn as a text box with the four answers unknowable.
                              {"default", "use-or-store"},
                              {"description", "Whether the reshape cache may be read and written: 'use' loads "
                                              "a stored combination and keeps nothing on a miss; "
                                              "'use-or-store' (default) does the same but keeps it; 'rebuild' "
                                              "always reshapes and stores over the old entry; 'ignore' always "
                                              "reshapes and stores nothing."}}},
                            {"cacheFile",
                             {{"type", "string"},
                              {"description", "Where to keep the entry: '' for the cache directory (default for "
                                              "a remote input), or the input file's own path to store in-file "
                                              "(default for a local input)."}}}}}},
          .dependsOn   = {group + "/open"},
          .order       = 2,
          .label       = "{{ binningName }} ({{ levels }})",
          // The reshape, as the call that made it: the arguments are the node's own, so the macro reshapes
          // the very navigator this combination reshaped — and leaves it for the user to walk.
          .macroLines    = {"auto * nav = ngnt->Reshape({{ binningName }}, {{ levels }});"},
          .macroIncludes = {"ndmspc/core/NGnNavigator.h"},
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
          // The layer the room is on, then the projection that layer shows: a click on a bin drills (the room
          // resolves the point and records it as this step's state), so the macro walks to the same place —
          // `nav->GetNavigator(point)` is that walk in one call. With no drill the recorded point is absent,
          // `{{ state.point }}` renders `{}` and the walk gives the navigator itself: a one-level navigator,
          // as before. Each node keeps its own point, so two maps on different layers stay apart.
          .macroLines  = {"auto * projection = nav->GetNavigator({{ state.point }})->GetProjection();"},
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
                                              "is omitted."}}},
                            // What the step records when it runs (see RecordSpectraCall): the parameter the
                            // macro's line is for (the first drawn), and the two values the room resolved for
                            // itself when the form left them out. The step's own bookkeeping, so its form never
                            // shows it — and the arguments the line reads.
                            {"parameter", {{"type", "string"}, {"hidden", true}}}}}},
          .dependsOn   = {group + "/map"},
          .order       = 4,
          .label       = "{{ parameters }}",
          // What this step is in C++, for a combination written out as a macro (`ndmspc/macro`): the spectra
          // the room obtained, as the list the library hands back — nothing is drawn, and the user goes on
          // with the list themselves. It is taken at the layer the step is on (the same walk the map's line
          // makes), because a drill into a layer moves where the spectra come from. The margin and mode are the
          // room's own (its defaults when the form sent none), recorded with the call (see RecordSpectraCall)
          // so the line reads what the room did rather than the library's defaults.
          .macroLines    = {"auto * spectra = nav->GetNavigator({{ state.point }})->DrawSpectraAll({{ parameter }}, { {{ axismargin }} }, {{ minmaxMode }});"},
          .macroIncludes = {"ndmspc/core/NGnNavigator.h"},
          // The tour of the whole workflow — open, reshape, map, spectra — carried by the last action of
          // the chain. Which tool carries it only decides where it sits; the steps name their own actions.
          // A tree with no parameters has no spectra to draw for, so the tour ends at the map (see
          // NgntWorkflowTour); the open action republishes it once it knows which tree is open.
          .tutorial    = NgntWorkflowTour(group),
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
          // This step is the layer the room is on, and it makes no object of its own: `macroSilent` says so,
          // rather than the export naming a step that has nothing to hand over or writing a line that would
          // have to guess at one. What it stands for is recorded as state (`state.point`, written by the drill
          // itself and by a spectra run) and read by the lines of the steps that do make objects.
          .macroSilent = true,
          // A helper, not a step a person creates: the drill calls it on the room's behalf, so it is
          // published and callable but never offered as a next step after the map.
          .explorer    = false,
      });

  // Every tool of this family is named the same way where a user reads it: said once, so a tool added to
  // this macro later is offered under it too rather than repeating the label on each registration.
  for (auto & entry : *(Ndmspc::gNdmspcMcpTools)) {
    if (entry.first.rfind(group + "/", 0) == 0) entry.second.groupLabel = groupLabel;
  }

  // ===========================================================================
  //  /api/ndmspc/ngnt/open — Open/close NGnTree files
  // ===========================================================================

  handlers[group + "/open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                 std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "ndmspc/ngnt";
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
      else {
        // The schema stays - it carries the defaults the reshapes so far set - but the parts that describe
        // *this file* are refreshed with it: the axes its picker names, and the homes "Store in" offers,
        // which are this file's own paths. Without that, opening another file would leave a dropdown
        // naming the previous one.
        const json built = BuildReshapeSchema(ngnt);
        for (const std::string name : {"axisLabels", "axisNames"}) {
          if (built.contains("properties") && built["properties"].contains("levels") &&
              built["properties"]["levels"].contains(name)) {
            ctx.Workspace()[reshapeKey]["properties"]["levels"][name] = built["properties"]["levels"][name];
          }
        }
        for (const std::string name : {"cacheFile"}) {
          if (built.contains("properties") && built["properties"].contains(name)) {
            for (const std::string field : {"enum", "labels", "default"}) {
              if (built["properties"][name].contains(field)) {
                ctx.Workspace()[reshapeKey]["properties"][name][field] = built["properties"][name][field];
              }
            }
          }
        }
      }
      wsOut["workspace"][reshapeKey] = ctx.Workspace()[reshapeKey];

      server->AddInputObject(ctx.ObjectName("ngnt"), ngnt);

      // Combinations already built for this file can be visited without reshaping, so a client can
      // offer them instead of guessing. Answered here, on open, because it is wanted **before** the
      // first reshape - after one, the reshape step has its own node and the client has already chosen.
      //
      // `prune: true` also clears out entries built from an older version of this file: the digest
      // carries the file's size and mtime, so those can never be named again. It is asked for rather
      // than done on every open - it deletes files, and it stats every source file in the cache.
      Ndmspc::NReshapeCache::Key openKey2;
      openKey2.FromTree(ngnt);
      if (httpIn.contains("prune") && httpIn["prune"].is_boolean() && httpIn["prune"].get<bool>()) {
        const auto pruned = Ndmspc::NReshapeCache::Prune(openKey2.file);
        httpOut["pruned"] = {{"removed", pruned.removed}, {"kept", pruned.kept}, {"bytesFreed", pruned.bytesFreed}};
      }
      httpOut["stored"] = json::parse(Ndmspc::NReshapeCache::List(openKey2.file), nullptr, false);

      // The workflow tour follows what the opened tree can do: with no parameters there is nothing to
      // draw spectra for, so it ends at the map (map last, no Save & Run after it) rather than offering a
      // step that could not run. The parameters live on the tree's `_params` entry, which `GetEntry`
      // loads, so it is read once here.
      Ndmspc::NParameters * params = nullptr;
      if (ngnt->GetEntries() > 0) {
        ngnt->GetEntry(0);
        params = ngnt->GetParameters();
      }
      const bool hasParams = params != nullptr && !params->GetNames().empty();
      if (Ndmspc::gNdmspcMcpTools != nullptr) {
        auto * tools   = Ndmspc::gNdmspcMcpTools;
        auto   spectra = tools->find("ndmspc/ngnt/spectra");
        auto   map     = tools->find("ndmspc/ngnt/map");
        // A tree with no parameters has no spectra step to offer: the tool is not offered to a person
        // (the Explorer's next step after the map), so the step is simply not there.
        if (spectra != tools->end()) spectra->second.explorer = hasParams;
        // The tour rides on the **last step the chain offers** — the spectra step when the tree has
        // parameters to draw for, the map when it has none. A tour left on a step that is not offered is a
        // tour nobody is shown: the Explorer lists what it offers and reads the tours out of those.
        const json tour = NgntWorkflowTour("ndmspc/ngnt", hasParams);
        if (spectra != tools->end()) spectra->second.tutorial = hasParams ? tour : json();
        if (map != tools->end()) map->second.tutorial = hasParams ? json() : tour;
      }
      // A tree with no parameters has no spectra step at all: drop any schema an earlier file left in
      // this session, so the step is not offered with an empty list to pick from.
      if (!hasParams) {
        ctx.Workspace().erase("spectra");
      }
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

    wsOut["group"] = "ndmspc/ngnt";
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
      // The form's default is the core count; a client that sends no count gets the same answer rather than a
      // silent 1, so "reshape with everything this process may use" holds however the request was made.
      const int defaultWorkers = std::max(1, static_cast<int>(Ndmspc::NUtils::AvailableCpuCount()));
      const int nWorkers       = std::max(1, ctx.GetInt("nWorkers", defaultWorkers));
      // Read before `nav` is dropped below: a session that already holds a navigator knows whether
      // parameters were being averaged, and that is part of what a stored combination is.
      const bool averages = nav ? nav->GetAverageParameters() : true;

      // What to do with the cache: read it? write it? The four modes answer both, and `refresh` is accepted
      // as the old name for a rebuild so a request written before them keeps working.
      std::string cacheModeName = ctx.GetString("cache", "");
      if (cacheModeName.empty() && ctx.GetParam("refresh", false)) cacheModeName = "rebuild";
      Ndmspc::NReshapeCache::CacheMode cacheMode = Ndmspc::NReshapeCache::CacheMode::kUseOrStore;
      if (!Ndmspc::NReshapeCache::ParseMode(cacheModeName, cacheMode)) {
        ctx.Result("Unknown reshape cache mode '" + cacheModeName +
                   "'; expected use, use-or-store, rebuild or ignore");
        return;
      }

      // Where an entry is kept. Absent means "the default for this kind of input" — in-file for local data,
      // the cache directory for remote data. An explicit "" is the cache directory, so the two cases have to
      // be told apart rather than both reading as "".
      const bool        cacheFileGiven = ctx.In().contains("cacheFile");
      const std::string cacheFileParam = ctx.GetString("cacheFile", "");

      if (nav) {
        server->RemoveInputObject(ctx.ObjectName("navigator"));
        nav = nullptr;
      }

      // The key names the combination; the home is separate, so the same entry is found wherever it is kept.
      Ndmspc::NReshapeCache::Key cacheKey;
      cacheKey.FromTree(ngnt);
      // "" means "the current definition"; store the resolved name so a map request, which never
      // receives one, resolves to the same entry instead of "".
      cacheKey.binning   = binningName.empty() ? ngnt->GetBinning()->GetCurrentDefinitionName() : binningName;
      cacheKey.levels    = levels;
      cacheKey.averages  = averages;
      cacheKey.cacheFile = cacheFileGiven ? cacheFileParam : Ndmspc::NReshapeCache::DefaultCacheFile(cacheKey.file);

      bool cacheHit = false;
      if (Ndmspc::NReshapeCache::Reads(cacheMode)) {
        nav = Ndmspc::NReshapeCache::Load(cacheKey, ngnt);
        cacheHit = nav != nullptr;
        if (cacheHit) {
          NLogInfo("[reshape] '%s' %s served from '%s'", binningName.c_str(), json(levels).dump().c_str(),
                   cacheKey.cacheFile.empty() ? "the cache directory" : cacheKey.cacheFile.c_str());
        }
      }

      if (nav == nullptr) {
        // A reshape over a real tree runs for minutes. Hold run output on for the duration so its
        // per-layer progress lands in the server's log while the request is still running, instead of
        // the one start line and one end line it used to print. The worker count is the request's own,
        // set for its duration only: whether threads help depends on how big one bin's content is, so it
        // is raised deliberately rather than by default.
        ScopedRunOutput runOutput(true);
        ScopedEnv       navWorkers("NDMSPC_NAV_WORKERS", std::to_string(nWorkers));
        nav = ngnt->Reshape(binningName, levels, 0, {}, {});
      }
      if (Ndmspc::NCancellation::IsCancelled()) {
        // The reshape was cancelled mid-way: the navigator is partial and belongs to no session, so
        // drop it and report a cancellation (the client stops waiting without an error notice). A
        // cancelled navigator is never stored — it is not the combination, only part of it.
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

      // Keep the built navigator, so the next request for this combination loads instead of reshaping.
      // Best-effort: a cache that cannot be written leaves the reshape it just served unaffected, and `ignore`
      // stores nothing at all.
      const bool stored = !cacheHit && Ndmspc::NReshapeCache::Writes(cacheMode)
                              ? Ndmspc::NReshapeCache::Store(cacheKey, nav)
                              : false;

      server->AddInputObject(ctx.ObjectName("navigator"), nav);

      if (!ctx.Workspace()[reshapeKey].contains("type")) {
        ctx.Workspace()[reshapeKey] = BuildReshapeSchema(ngnt);
      }

      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "levels", levels);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "binningName", binningName);
      Ndmspc::NSchemaBuilder::SetDefault(ctx.Workspace()[reshapeKey], "nWorkers", nWorkers);
      wsOut["workspace"][reshapeKey] = ctx.Workspace()[reshapeKey];

      if (!ctx.Workspace()[mapKey].contains("type")) {
        ctx.Workspace()[mapKey] = BuildMapSchema();
      }
      // No `binningName` default for the map step: it draws the navigator it is handed, so it has no binning
      // field of its own (`BuildMapSchema` declares none) and a default written here could never land — it
      // only logged "property 'binningName' is not declared in the schema, ignoring default" on every reshape.
      wsOut["workspace"][mapKey] = ctx.Workspace()[mapKey];

      // Additive, so a client that does not know them is unaffected: which of load/reshape stored happened,
      // and where the entry went, explain why the progress bars did or did not appear.
      if (cacheHit) httpOut["cached"] = true;
      if (stored) httpOut["stored"] = true;
      if (cacheHit || stored) {
        httpOut["home"] = cacheKey.cacheFile.empty() ? "cache" : (cacheKey.cacheFile == cacheKey.file ? "in-file" : "cache-file");
      }

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

    wsOut["group"] = "ndmspc/ngnt";
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

      json nested;
      if (nav->GetLevel() == 0) {
        // Exporting the whole navigator to JSON is what a map request spends its time on, so the
        // result is kept beside the navigator and re-read instead of rebuilt. The key has to carry
        // `averages` too: a per-request override changes what the export contains, so the two must not
        // share a kept copy.
        Ndmspc::NReshapeCache::Key mapKeyDef;
        mapKeyDef.FromTree(ngnt);
        // The navigator is what is being exported, so its own levels are what the export depends on -
        // read from the object rather than from the reshape step's workspace, which may since have been
        // edited to a combination this navigator is not. The binning name has to match the one the
        // reshape stored under, or this looks in a different entry and never finds the kept export.
        mapKeyDef.levels   = nav->GetLevels();
        mapKeyDef.binning  = ngnt->GetBinning()->GetCurrentDefinitionName();
        mapKeyDef.averages = averages;
        // The home the navigator was reshaped into, so the export is looked for beside it. The map step has
        // no cache fields of its own — it draws the navigator it is handed — so this follows the reshape's
        // default for the kind of input.
        mapKeyDef.cacheFile = Ndmspc::NReshapeCache::DefaultCacheFile(mapKeyDef.file);

        std::string kept;
        if (Ndmspc::NReshapeCache::LoadMapJson(mapKeyDef, kept)) {
          nested = json::parse(kept, nullptr, false);
          if (nested.is_discarded()) {
            NLogWarning("[map] the kept export could not be parsed; exporting again");
            nested = json::object();
          }
          else {
            // At Info so the saving is visible in a room's log: a map that reads the kept export
            // explains why it did not spend its time exporting.
            NLogInfo("[map] served the export kept for this combination (%zu bytes)", kept.size());
          }
        }

        if (nested.is_null() || nested.empty()) {
          json exportCfg;
          exportCfg["averages"] = averages;
          nav->ExportToJson(nested, nav, std::vector<std::string>{}, exportCfg);
          // Keep it, but only if it looks usable — a failed export must not be remembered.
          if (!nested.empty()) Ndmspc::NReshapeCache::StoreMapJson(mapKeyDef, nested.dump());
        }

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
      if (RenderMapLayers(ctx, nav, mappingPad, json::array(), /*replace=*/true, nested) == 0) {
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

      // The spectra step is offered only when there is something to draw for: a tree with no parameters
      // has nothing to pick, so its schema is not published and the step is not offered — the workflow
      // ends at the map (see NgntWorkflowTour). The parameters come from the tree; a navigator that has
      // its own (a tree opened without `InitParameters`) counts too.
      Ndmspc::NParameters * nparams   = ngnt->GetParameters();
      const bool            hasParams = (nparams != nullptr && !nparams->GetNames().empty()) ||
                             !nav->GetParameterNames().empty();
      if (hasParams) {
        if (!ctx.Workspace()[spectraKey].contains("type")) {
          ctx.Workspace()[spectraKey] = BuildSpectraSchema(ngnt);
        }
        wsOut["workspace"][spectraKey] = ctx.Workspace()[spectraKey];
      }

      // Where this step stands, so its line can walk there (see RecordLayerPoint): a map makes its projection on
      // the navigator it was given, so with nothing drilled it is on the navigator itself.
      RecordLayerPoint(ctx);

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
        // The layers from where the drill landed down, joined to what the pad already holds
        // (`replace=false`): the layer that was clicked stays as a tab, and only a fresh map owns the pad.
        const std::string mappingPad = PadArg(ctx, "mappingPad", "pad1");
        // A drill is "show me the layer I went into": `reveal` moves the pad to this drawing's first
        // layer, so a click opens the deeper projection rather than leaving the user on the tab they
        // clicked. A fresh map does not ask for it - its first layer is new and shows on its own.
        wsOut["payload"]["reveal"] = true;
        RenderMapLayers(ctx, navCurrent, mappingPad, point, /*replace=*/false);

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

    wsOut["group"] = "ndmspc/ngnt";
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
      // A tree with no parameters has nothing to draw spectra for: the workflow ends at the map (the tour
      // is trimmed to it, see the open action), so this is not an error - the step simply draws nothing.
      if (parameters.empty()) {
        NLogWarning("No parameters to draw spectra for; nothing to do");
        ctx.Success();
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

      // The spectra are drawn for the last level's bins, so the point only has to name a bin for the
      // levels above it. A fresh run has no point yet, and a bin the point names (or its default, the
      // first) may be an empty one - the navigator builds a child only for a bin with data - so a
      // missing child falls back to the level's first bin that does have one, rather than refusing.
      // An entry for the last level, if the point carries one, names the spectra themselves and is not
      // used here.
      //
      // A bin is a ROOT bin number (1-based; see NGnNavigator::Reshape, `indexInProj = FindFixBin`), so
      // 0 is no bin: a level the point leaves at 0 or does not reach starts from bin 1.
      // The point the request asked for (`point`/`level`/`args.bin` - what a click carries), else the
      // stored one: the same resolution a PATCH uses, so a spectra step a click created draws the point
      // that was clicked rather than the empty default.
      std::vector<int>       point      = ResolveDrillPoint(ctx, ctx.In());
      const size_t           nLevels    = nav->GetNLevels();
      Ndmspc::NGnNavigator * navCurrent = nav;

      const size_t needed = nLevels > 0 ? nLevels - 1 : 0;
      point.resize(needed, 1); // drop a last-level entry; a missing level starts at the first bin

      for (size_t iLevel = 0; iLevel < needed; iLevel++) {
        const int bin = point[iLevel] < 1 ? 1 : point[iLevel];
        Ndmspc::NGnNavigator * next = navCurrent->GetChild(static_cast<size_t>(bin));
        if (next == nullptr) {
          // The named bin holds no data (a child is built for a bin with data only): take the level's
          // first bin that does, and name it in the point.
          size_t firstBin = 0;
          next            = navCurrent->GetFirstChild(&firstBin);
          if (next != nullptr) point[iLevel] = static_cast<int>(firstBin);
        }
        if (next == nullptr) {
          NLogTrace("No navigator found for spectra at point: %s", json(point).dump().c_str());
          ctx.Result("No navigator found for spectra at point: " + json(point).dump());
          return;
        }
        navCurrent = next;
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

      // What the room is doing, on the step: the parameter, the two values its macro line reads and the layer
      // it drew for (see RecordSpectraCall) — the layer only here, after the walk above, since that is what
      // settles it (a point may name a bin that holds no data, and the walk moves to the level's first bin
      // that does).
      RecordSpectraCall(ctx, parameters, minmax, minmaxMode, point);

      int padIndex = ParsePadIndex(spectraPad);
      // A fresh draw owns the pads it draws to (`replace`), so spectra from another combination do not
      // linger; the PATCH path appends.
      RenderSpectra(ctx, navCurrent, parameters, minmax, minmaxMode, padIndex, point, true, /*replace=*/true);

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

      // A drill draws spectra for the same parameter the step is on: the step keeps reading what it drew,
      // including the layer the drill landed on.
      RecordSpectraCall(ctx, parameters, minmax, minmaxMode, point);

      RenderSpectra(ctx, navCurrent, parameters, minmax, minmaxMode, padIndex, point);
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

    wsOut["group"] = "ndmspc/ngnt";
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
      clickAction["path"]        = "ndmspc/ngnt/point";
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
