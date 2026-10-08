///
/// toolBrowser.C — a TBrowser-like ROOT file browser, as a tool group.
///
/// Registers: browser/open, browser/browse (the combination) and the internal rbrowser/ls,
/// rbrowser/draw, rbrowser/sparse, rbrowser/project (hidden helpers the tree drives).
///
/// URLs:  /api/browser/open, /api/browser/browse, /api/rbrowser/ls, /api/rbrowser/draw,
///        /api/rbrowser/sparse, /api/rbrowser/project
///
/// The tool does everything the browser needs: it opens a ROOT file, walks its keys and directories to
/// build the tree, and draws a chosen object into a pad. The visible part is a **combination** —
/// `browse` depends on `open` — so the Explorer shows the group with `open` as the action that starts
/// it and, under it, the `browse` step:
///
///   open ──▶ browse (the file tree)  ──expand──▶ rbrowser/ls   ──click──▶ rbrowser/draw ──▶ a jsroot pad
///
/// A `THnSparse` is the exception: jsroot cannot draw one, and projecting it needs choices (which
/// axes, which range). Clicking one opens a **dialog** instead — `rbrowser/sparse` lists the object's
/// axes in a form, and its submit runs `rbrowser/project`, which projects the chosen 1–3 axes into a
/// `TH1`/`TH2`/`TH3` and draws that. The dialog is the generic `payload.dialog` envelope a tool
/// declares and the UI renders, so nothing here needs a THnSparse-specific widget:
///
///   ──click──▶ rbrowser/sparse ──▶ (dialog: use/min/max/rebin per axis) ──submit──▶ rbrowser/project ──▶ a jsroot pad
///
/// `browse`'s form is the file tree: its `key` field is a `format: "tree"` whose nodes come from the
/// file, so the browse step is where you browse and draw — expanding a folder loads its children,
/// clicking an object draws it. Clicking records nothing: it just draws. That is why `ls` and `draw`
/// live in the separate `rbrowser` group — the server records a node for every POST of an action in a
/// combination group (a dependency-less one becomes a *root*), so an action that must not add a step
/// cannot be part of `browser`. They are hidden, so neither the Explorer nor the Tools panel lists
/// them; the tree's nodes invoke them by name over the websocket.
///
/// The argument a node names is called `key` (the ROOT key path) rather than `path`, because `path` is
/// the server's own combination address (the node ids a request acts on) and is consumed by the
/// dispatch; a tool argument of that name would be dropped from a node's recorded arguments.
///
/// `file` may be a local path or an http(s) URL. A URL is read by ROOT itself (`TFile::Open` ->
/// `TDavixFile`/`TCurlFile`), which needs the matching ROOT net package installed (`root-net-curl` on
/// Fedora >= 45, `root-net-davix` otherwise, alongside `root-net-http`); without it ROOT's
/// `TFile::Open` fails on a URL and the tool reports that as an open failure.
///
/// Usage:
///   ndmspc-server -m "macros/tools/toolNgnt.C,macros/tools/toolBrowser.C"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <TAxis.h>
#include <TBranch.h>
#include <TCanvas.h>
#include <TClass.h>
#include <TColor.h>
#include <TDirectory.h>
#include <TObjString.h>
#include <TFile.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <TKey.h>
#include <TList.h>
#include <TObject.h>
#include <TObjArray.h>
#include <TSystem.h>
#include <TTree.h>

#include <ndmspc/core/NUtils.h>
#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NRouteContext.h>
#include <ndmspc/http/NSchemaBuilder.h>

namespace {

/// The name the opened file is registered under in the server's object map.
const char * kFileObject = "browserFile";
/// Workspace routes (short keys: the action name without its group).
const char * kOpenKey   = "open";
const char * kBrowseKey = "browse";
/// The internal helper group. It declares no `dependsOn`, so the server keeps no combination node for
/// it: expanding and drawing are the tree's own doing, not steps of the analysis.
const char * kInternals = "rbrowser";
/// How many curves a named overlay canvas keeps; projecting one more drops the oldest (a canvas of many
/// curves is memory, and an unreadable picture). The deployment may set `NDMSPC_BROWSER_MAX_CURVES`;
/// anything that is not a positive number falls back to 10.
int MaxOverlayCurves()
{
  static const int value = [] {
    const char * raw = gSystem != nullptr ? gSystem->Getenv("NDMSPC_BROWSER_MAX_CURVES") : nullptr;
    if (raw == nullptr || *raw == '\0') return 10;
    const int parsed = std::atoi(raw);
    return parsed > 0 ? parsed : 10;
  }();
  return value;
}

/// The key name without its cycle number (`hpx;1` -> `hpx`), which is what `TFile::Get` wants.
std::string StripCycle(const std::string & name)
{
  const auto pos = name.find(';');
  return pos == std::string::npos ? name : name.substr(0, pos);
}

/// Join a parent key and a child name into the key the tool uses (`dir/hist`).
std::string ChildKey(const std::string & prefix, const std::string & name)
{
  return prefix.empty() ? name : prefix + "/" + name;
}

/// Release an object read from a file once a request is done with it. `TFile::Get` holds what it reads
/// until the file closes, and a `THnSparse` is big — so a request frees it rather than leaving one per
/// projection in memory. A later `Get` re-reads it from disk.
void Release(TObject * object)
{
  delete object;
}

/// Whether a key's class is a TTree (or a TNtuple, a TTree subclass).
bool IsTreeClass(const std::string & className)
{
  TClass * cl = TClass::GetClass(className.c_str());
  return cl != nullptr && cl->InheritsFrom(TTree::Class());
}

/// Whether a key's class is a directory (`TDirectoryFile`, …), so it can be descended into.
///
/// Deliberately not `TKey::IsFolder()`: ROOT reports a TTree-derived key (a TNtuple) as a folder too,
/// which would send it down the directory path and hide its branches.
bool IsDirectoryClass(const std::string & className)
{
  TClass * cl = TClass::GetClass(className.c_str());
  return cl != nullptr && cl->InheritsFrom(TDirectory::Class());
}

/// Whether a key's class is a THnSparse (a `THnSparseT<T>`), which jsroot cannot draw directly.
///
/// A sparse key is a leaf in the tree: clicking it opens the projection dialog rather than drawing,
/// because there is nothing to draw until an axis (or three) has been projected out of it.
bool IsSparseClass(const std::string & className)
{
  // A sparse key's class name is the template one (`THnSparseT<TArrayF>`), which TClass resolves; the
  // prefix is a belt-and-braces match for a name the dictionary may not have loaded yet.
  if (className.rfind("THnSparse", 0) == 0) return true;
  TClass * cl = TClass::GetClass(className.c_str());
  return cl != nullptr && cl->InheritsFrom(THnSparse::Class());
}

/// The action a click on a folder dispatches: expand it.
json LsAction(const std::string & key)
{
  return Ndmspc::NRouteContext::Action(std::string(kInternals) + "/ls", "POST", json{{"key", key}});
}

/// The action a click on an object dispatches: draw it.
json DrawAction(const std::string & key)
{
  return Ndmspc::NRouteContext::Action(std::string(kInternals) + "/draw", "POST", json{{"key", key}});
}

/// The action a click on a tree branch dispatches: draw a histogram of it.
json DrawBranchAction(const std::string & key, const std::string & branch)
{
  return Ndmspc::NRouteContext::Action(std::string(kInternals) + "/draw", "POST",
                                       json{{"key", key}, {"branch", branch}});
}

/// The action a click on a THnSparse dispatches: open its projection dialog.
json SparseAction(const std::string & key)
{
  return Ndmspc::NRouteContext::Action(std::string(kInternals) + "/sparse", "POST",
                                       json{{"key", key}});
}

/// The signature of a THnSparse's axes - name, title, bin count and bounds, in order. Two objects with
/// the same axes (the unlike-sign and mixed-event spectra of one analysis, say) share it, which is what
/// lets a saved projection configuration be applied to either.
std::string SparseSignature(THnSparse * sparse)
{
  std::ostringstream out;
  const int          ndim = sparse != nullptr ? sparse->GetNdimensions() : 0;
  out << "ndim=" << ndim;
  for (int i = 0; i < ndim; i++) {
    TAxis * axis = sparse != nullptr ? sparse->GetAxis(i) : nullptr;
    if (axis == nullptr) continue;
    out << '|' << axis->GetName() << ';' << axis->GetTitle() << ';' << axis->GetNbins() << ';'
        << axis->GetXmin() << ',' << axis->GetXmax();
  }
  return out.str();
}

/// The saved projection configuration for a signature (`{axes, drawOpts}`), or null when none is kept.
json SparseSaved(Ndmspc::NRouteContext & ctx, const std::string & signature)
{
  json & state = ctx.State();
  if (!state.contains("browser") || !state["browser"].is_object()) return json();
  const json & browser = state["browser"];
  if (!browser.contains("sparse") || !browser["sparse"].is_object()) return json();
  const json & sparse = browser["sparse"];
  if (!sparse.contains(signature)) return json();
  return sparse[signature];
}

/// Keep a projection configuration under its signature, so the next dialog - for this object or another
/// with the same axes - opens on it.
void StoreSparse(Ndmspc::NRouteContext & ctx, const std::string & signature, const json & entry)
{
  ctx.State()["browser"]["sparse"][signature] = entry;
}

/// The projections kept for an opened file — every one drawn from it, "same canvas" or not. Keyed by
/// the file, not the request's session: a projection runs as an internal action whose session can
/// differ from the one the file was opened under, and a session-keyed list would be a fresh (empty) one
/// every time. The server owns the list, and the list owns the projections (a canvas only references
/// them, so they must outlive each request).
TList * OverlayList(Ndmspc::NRouteContext & ctx, const std::string & file)
{
  const std::string name = "browserOverlay:" + file;
  if (TList * existing = ctx.GetObject<TList>(name)) return existing;

  Ndmspc::NHttpServer * server = ctx.Server();
  if (server == nullptr) return nullptr;

  auto * list = new TList();
  list->SetOwner(true);
  server->AddInputObject(name, list);
  return list;
}

/// The signatures already kept in a file's overlay list, so a projection that is already there is not
/// added again: the server re-runs a step's handler when it makes a combination live, and a replayed
/// projection must not pile a twin onto the canvas.
TList * OverlaySeen(Ndmspc::NRouteContext & ctx, const std::string & file)
{
  const std::string name = "browserOverlaySeen:" + file;
  if (TList * existing = ctx.GetObject<TList>(name)) return existing;

  Ndmspc::NHttpServer * server = ctx.Server();
  if (server == nullptr) return nullptr;

  auto * list = new TList();
  list->SetOwner(true);
  server->AddInputObject(name, list);
  return list;
}

/// The color a curve gets by its position on the overlay, so the overlaid projections are told apart.
int OverlayColor(int index)
{
  static const std::vector<int> colors = {kRed,       kBlue,     kGreen + 2, kMagenta + 1,
                                          kOrange + 7, kCyan + 2, kViolet,    kAzure + 2};
  return colors[index % static_cast<int>(colors.size())];
}

/// Build a canvas from the overlay list: the first curve draws the axes, every one after it overlays
/// ("same"), each in its own colour. ROOT composes the picture — the pad only draws the finished canvas
/// — and the caller deletes it once `ShowRoot` has serialized it (the projections belong to the list,
/// so deleting the canvas leaves them alone). Rebuilt from the list each time, so it is idempotent and
/// a replayed step changes nothing.
TCanvas * OverlayCanvas(const TList * overlay, const std::string & drawOpts)
{
  auto * canvas = new TCanvas("ndmspc_overlay", "Projection", 800, 600);
  canvas->cd();

  Int_t drawn = 0;
  for (Int_t i = 0; i < overlay->GetEntries(); i++) {
    auto * hist = dynamic_cast<TH1 *>(overlay->At(i));
    if (hist == nullptr) continue;

    hist->SetStats(false);
    hist->SetLineColor(OverlayColor(drawn));
    const std::string option =
        (drawn == 0) ? drawOpts : (drawOpts.empty() ? "same" : drawOpts + " same");
    hist->Draw(option.c_str());
    drawn++;
  }
  return canvas;
}

/// The projection form for a THnSparse: a table with one row per axis (project?, min, max, rebin) plus
/// draw options. The table is a generic form field (`format:"table"`), so the tool asks a gridful of
/// choices at once without the UI knowing what for. `saved` is a previously kept configuration for the
/// same axis signature, whose values overlay the built-in defaults.
json SparseDialogSchema(THnSparse * sparse, const json & saved = json())
{
  const int  ndim      = sparse != nullptr ? sparse->GetNdimensions() : 0;
  const json savedRows = (saved.is_object() && saved.contains("axes") && saved["axes"].is_array())
                             ? saved["axes"]
                             : json::array();

  // The checkbox leads the line, the axis name is the label, and the options follow it. The label is
  // the axis **name** (`mean`), not its title: a name is what identifies an axis, and the title is
  // prose written for a reader ("Mean [GeV]") that a row in a table does not need - and plain text, so
  // a name carrying an underscore is not read as LaTeX.
  json columns = json::array({
      {{"key", "use"}, {"title", "Project"}, {"type", "boolean"}},
      {{"key", "axis"}, {"title", "Axis"}, {"type", "string"}, {"readOnly", true}},
      {{"key", "min"}, {"title", "min"}, {"type", "number"}},
      {{"key", "max"}, {"title", "max"}, {"type", "number"}},
      {{"key", "rebin"}, {"title", "rebin"}, {"type", "integer"}},
  });

  json rows = json::array();
  for (int i = 0; i < ndim; i++) {
    TAxis *      axis = sparse->GetAxis(i);
    const char * name = (axis != nullptr && axis->GetName() != nullptr) ? axis->GetName() : "";

    // "index, name" - the index places the axis where the projection will put it, the name is what the
    // file calls it.
    const std::string label = std::to_string(i) + ", " + name;

    // The bounds default to the axis' own, so the table opens on the full range and the user narrows it.
    const double xmin  = axis != nullptr ? axis->GetXmin() : 0.0;
    const double xmax  = axis != nullptr ? axis->GetXmax() : 0.0;
    bool         use   = i == 0;
    int          rebin = 1;
    double       lo    = xmin;
    double       hi    = xmax;

    // A saved row for this axis overlays the defaults, its bounds clamped into the axis (and dropped
    // back to full if they no longer make a range).
    if (i < static_cast<int>(savedRows.size()) && savedRows[i].is_object()) {
      const json & row = savedRows[i];
      if (row.contains("use") && row["use"].is_boolean()) use = row["use"];
      if (row.contains("rebin") && row["rebin"].is_number_integer()) rebin = row["rebin"];
      const double savedLo = row.value("min", xmin);
      const double savedHi = row.value("max", xmax);
      if (savedHi > savedLo) {
        lo = std::max(xmin, savedLo);
        hi = std::min(xmax, savedHi);
      }
    }
    if (!(hi > lo)) {
      lo = xmin;
      hi = xmax;
    }

    rows.push_back({{"axis", label}, {"use", use}, {"min", lo}, {"max", hi}, {"rebin", rebin}});
  }

  // A rebin ROOT cannot do is not offered: `Rebin` works on fixed bin widths only, so an axis with
  // variable-width bins gets its `rebin` cell disabled rather than accepting a number that would be
  // ignored. The table carries that as a per-row list of the cells that are off (see the UI's table
  // field). `GetXbins()` answers every axis — two edges for a uniform one — so the count tells them
  // apart.
  for (int i = 0; i < static_cast<int>(rows.size()) && i < ndim; i++) {
    TAxis *        axis  = sparse->GetAxis(i);
    const TArrayD * edges = axis == nullptr ? nullptr : axis->GetXbins();
    if (edges == nullptr || edges->GetSize() <= 2) continue;
    rows[i]["disabled"] = json::array({"rebin"});
  }

  json properties = json::object();
  properties["axes"] = {{"type", "array"},
                        {"format", "table"},
                        {"title", "Axes"},
                        {"description",
                         "Tick one to three axes to project. min/max cut any axis (a cut on a "
                         "non-projected axis narrows the sample); rebin applies to a projected one."},
                        {"columns", columns},
                        {"default", rows}};

  json drawOpts = {{"type", "string"},
                   {"title", "Draw options"},
                   {"description", "jsroot options, e.g. 'colz' for a 2D projection."}};
  if (saved.is_object() && saved.contains("drawOpts") && saved["drawOpts"].is_string() &&
      !saved["drawOpts"].get<std::string>().empty()) {
    drawOpts["default"] = saved["drawOpts"];
  }
  properties["drawOpts"] = drawOpts;

  // The projection's name: its own tab, and the histogram's name. It is kept with the rest of the
  // configuration, so the last name used comes back; it starts at "projection", which projects share
  // unless the user names them differently to get a tab of their own.
  std::string nameDefault = "projection";
  if (saved.is_object() && saved.contains("name") && saved["name"].is_string() &&
      !saved["name"].get<std::string>().empty())
    nameDefault = saved["name"];
  properties["name"] = {{"type", "string"},
                        {"title", "Name"},
                        {"description", "The projection's name — its own tab and histogram name."},
                        {"default", nameDefault}};

  // SAME is the user's own choice, remembered: it starts unticked (a projection drawn as its own object
  // starts a new canvas) and stays as they leave it — once ticked, every later projection overlays onto
  // the canvas in hand until they untick it, which starts another.
  bool overlay = false;
  if (saved.is_object() && saved.contains("same") && saved["same"].is_boolean())
    overlay = saved["same"];
  json same = {{"type", "boolean"},
               {"title", "Same canvas"},
               {"default", overlay},
               {"description", "Use ROOT's SAME draw option, onto the canvas in hand."}};
  properties["same"] = same;

  // The form shows the fields in this order (a JSON object's keys would come out sorted): the name
  // first, then the axes it projects.
  return json{{"type", "object"}, {"order", {"name", "axes", "drawOpts", "same"}}, {"properties", properties}};
}

/// A TTree's branches as nodes that draw themselves: a click makes `draw` project the branch into a
/// histogram (the tree itself, clicked on its own row, shows jsroot's tree view instead).
json BranchNodes(TTree * tree, const std::string & key)
{
  json nodes = json::array();
  if (tree == nullptr) return nodes;
  TObjArray * branches = tree->GetListOfBranches();
  if (branches == nullptr) return nodes;

  for (int i = 0; i < branches->GetEntries(); i++) {
    TBranch * branch = dynamic_cast<TBranch *>(branches->At(i));
    if (branch == nullptr) continue;
    json node;
    node["id"]     = ChildKey(key, branch->GetName());
    node["label"]  = branch->GetName();
    node["detail"] = branch->GetTitle() != nullptr ? branch->GetTitle() : "";
    node["action"] = DrawBranchAction(key, branch->GetName());
    nodes.push_back(node);
  }
  return nodes;
}

/// One level of a directory: a node per key. A folder recurses only into the paths the caller has
/// expanded; a TTree lists its branches when expanded. A leaf's action draws it.
json ListDirectory(TDirectory * dir, const std::string & prefix, const std::set<std::string> & expanded)
{
  json nodes = json::array();
  if (dir == nullptr) return nodes;
  TList * keys = dir->GetListOfKeys();
  if (keys == nullptr) return nodes;

  for (TObject * obj : *keys) {
    TKey * key = dynamic_cast<TKey *>(obj);
    if (key == nullptr) continue;

    const std::string name   = key->GetName();
    const std::string path   = ChildKey(prefix, name);
    const bool        folder = IsDirectoryClass(key->GetClassName());
    const bool        tree   = !folder && IsTreeClass(key->GetClassName());
    const bool        sparse = !folder && !tree && IsSparseClass(key->GetClassName());

    json node;
    node["id"]     = path;
    node["label"]  = name + ";" + std::to_string(key->GetCycle());
    node["detail"] = key->GetClassName();

    if (folder) {
      node["expandable"] = true;
      node["action"]     = LsAction(path);
      node["loadAction"] = LsAction(path);
      if (expanded.count(path) != 0) {
        TDirectory * sub = dir->GetDirectory(name.c_str());
        if (sub != nullptr) {
          node["children"] = ListDirectory(sub, path, expanded);
          node["loaded"]   = true;
        }
      }
    }
    else if (tree) {
      node["expandable"] = true;
      // Clicking the tree draws it (jsroot's tree view); the chevron lists its branches.
      node["action"]     = DrawAction(path);
      node["loadAction"] = LsAction(path);
      if (expanded.count(path) != 0) {
        TTree * t        = dynamic_cast<TTree *>(dir->Get(name.c_str()));
        json    children = BranchNodes(t, path);
        if (!children.empty()) {
          node["children"] = children;
          node["loaded"]   = true;
        }
      }
    }
    else if (sparse) {
      // A THnSparse has no jsroot renderer, so a click opens the projection dialog rather than drawing
      // the object itself; the dialog lists its axes and projects the chosen ones.
      node["action"] = SparseAction(path);
    }
    else {
      node["action"] = DrawAction(path);
    }
    nodes.push_back(node);
  }
  return nodes;
}

/// The tree value the `browse` step's `key` field reads.
json BuildTree(TFile * file, const std::set<std::string> & expanded)
{
  json tree;
  tree["root"]  = file != nullptr ? std::string(file->GetName()) : std::string();
  tree["nodes"] = ListDirectory(file, "", expanded);
  return tree;
}

/// The paths currently expanded, kept in the server's own state so it survives across requests.
std::set<std::string> ExpandedOf(Ndmspc::NRouteContext & ctx)
{
  std::set<std::string> out;
  json &                state = ctx.State();
  if (state.is_object() && state.contains("browser")) {
    json & expanded = state["browser"]["expanded"];
    if (expanded.is_array()) {
      for (const auto & item : expanded) {
        if (item.is_string()) out.insert(item.get<std::string>());
      }
    }
  }
  return out;
}

/// Store the expanded paths back into the server's state.
void StoreExpanded(Ndmspc::NRouteContext & ctx, const std::set<std::string> & expanded)
{
  json arr = json::array();
  for (const auto & path : expanded) arr.push_back(path);
  ctx.State()["browser"]["expanded"] = arr;
}

/// Publish the file into `browser/open`, so the open step's form starts on the file that was opened.
void PublishOpenSchema(Ndmspc::NRouteContext & ctx, const std::string & file)
{
  json & route  = ctx.Workspace()[kOpenKey];
  route["type"] = "object";
  json & prop   = route["properties"]["file"];
  prop["type"]    = "string";
  prop["default"] = file;
  ctx.WsOut()["workspace"][kOpenKey] = route;
}

/// Publish the tree into `browser/browse`'s `key` field, so the browse step's form is the file tree — a
/// `format: "tree"` field whose nodes come from the file. Expanding a node runs `rbrowser/ls`, clicking
/// one runs `rbrowser/draw` (the node's own action), so browsing and drawing happen in the step itself.
/// `selected` is the key the field starts on ("" for none).
void PublishTreeSchema(Ndmspc::NRouteContext & ctx, const json & tree, const std::string & selected)
{
  json & route  = ctx.Workspace()[kBrowseKey];
  route["type"] = "object";
  json & prop   = route["properties"]["key"];
  prop["type"]        = "string";
  prop["format"]      = "tree";
  prop["title"]       = "Object";
  prop["description"] = "The file's objects: expand a folder, or click an object to draw it.";
  prop["nodes"]       = tree["nodes"];
  if (selected.empty()) {
    prop.erase("default");
  }
  else {
    prop["default"] = selected;
  }
  ctx.WsOut()["workspace"][kBrowseKey] = route;
}

/// The opened file, or nullptr when none is open.
TFile * OpenFileOf(Ndmspc::NRouteContext & ctx)
{
  // The file belongs to this request's session, so two browser sessions keep one each.
  return ctx.GetObject<TFile>(ctx.ObjectName(kFileObject));
}

/// Drop every overlay kept for a file — its list and its signatures, one pair per projection name
/// (`browserOverlay:<file>|<name>`). They are the file's own, so closing it (or opening another) frees
/// the projections they hold rather than leaving them behind.
void DropOverlays(Ndmspc::NRouteContext & ctx, const std::string & file)
{
  Ndmspc::NHttpServer * server = ctx.Server();
  if (server == nullptr) return;

  const std::string        prefix = file + "|";
  std::vector<std::string> doomed;
  for (const auto & entry : server->GetObjectsMap()) {
    for (const char * kind : {"browserOverlay:", "browserOverlaySeen:"}) {
      if (entry.first.rfind(std::string(kind) + prefix, 0) == 0) {
        doomed.push_back(entry.first);
        break;
      }
    }
  }
  for (const auto & key : doomed) server->RemoveInputObject(key);
}

/// Drop the opened file (the server deletes it), and the overlays that were its own.
void CloseBrowserFile(Ndmspc::NRouteContext & ctx)
{
  if (auto * server = ctx.Server()) {
    if (TFile * file = OpenFileOf(ctx)) DropOverlays(ctx, file->GetName());
    server->RemoveInputObject(ctx.ObjectName(kFileObject));
  }
}

/// The open file with the tree re-published, or a failure written to `ctx` when none is open.
TFile * RequireOpenFile(Ndmspc::NRouteContext & ctx)
{
  TFile * file = OpenFileOf(ctx);
  if (file == nullptr || file->IsZombie()) {
    ctx.Result("No ROOT file is open; run browser/open first");
    return nullptr;
  }
  return file;
}

} // namespace

void toolBrowser()
{
  auto &      handlers = *(Ndmspc::gNdmspcHttpHandlers);
  std::string group    = "browser";

  // ===========================================================================
  //  MCP tool metadata. Descriptions live here in the macro, not in C++, so
  //  they can be changed without recompiling the server.
  // ===========================================================================
  Ndmspc::RegisterMcpTool(group + "/open", {
      .description = "Open a ROOT file (a local path or an http(s) URL) and show its directory/object "
                     "tree in the browse step. GET reports the open file, DELETE closes it.",
      .methods     = {"GET", "POST", "DELETE"},
      .inputSchema = {{"properties",
                       {{"file",
                         {{"type", "string"},
                          {"description", "Path or URL of the ROOT file to open."},
                          {"default", "https://root.cern/js/files/hsimple.root"}}}}}},
      .order       = 1,
      .label       = "{{ file }}",
      .session     = true, // the opened file is part of the room's session
  });

  Ndmspc::RegisterMcpTool(group + "/browse", {
      .description = "Browse the open file: the step's form is its tree. Expand a folder to load its "
                     "children, click an object to draw it.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"key",
                         {{"type", "string"},
                          {"format", "tree"},
                          {"title", "Object"},
                          {"description",
                           "The file's objects: expand a folder, or click an object to draw it."}}}}}},
      .dependsOn   = {group + "/open"},
      .order       = 2,
      .label       = "{{ key }}",
      .runButton   = false, // the tree is how it works: clicking an object draws it, so there is no Run
  });

  // The internals the tree drives. They are a group of their own with no `dependsOn`, so the server
  // keeps no node for them (a POST in a combination group would add a step — or, with no dependency, a
  // root). Hidden: neither the Explorer nor the Tools panel lists them.
  Ndmspc::RegisterMcpTool(std::string(kInternals) + "/ls", {
      .description = "Expand a folder of the open file and re-send the whole tree with its children. "
                     "Internal: the browse step's tree drives it.",
      .methods     = {"POST"},
      .hidden      = true,
      .inputSchema = {{"properties",
                       {{"key", {{"type", "string"}, {"description", "ROOT key path of the folder to expand."}}}}}},
  });

  Ndmspc::RegisterMcpTool(std::string(kInternals) + "/draw", {
      .description = "Draw an object of the open file. Internal: the browse step's tree drives it. The "
                     "object names no pad, so the pad view decides where it lands (its fixed pad, or the "
                     "rotating ones).",
      .methods     = {"POST", "PATCH"},
      .hidden      = true,
      .inputSchema = {{"properties",
                       {{"key", {{"type", "string"}, {"description", "ROOT key path within the opened file."}}},
                        {"branch",
                         {{"type", "string"},
                          {"description",
                           "Branch of the tree at 'key' to project into a histogram (optional)."}}},
                        {"drawOpts",
                         {{"type", "string"}, {"description", "jsroot draw options, e.g. 'colz'."}}}}}},
  });

  Ndmspc::RegisterMcpTool(std::string(kInternals) + "/sparse", {
      .description = "Open the projection dialog for a THnSparse: it lists the object's axes and their "
                     "bounds, and its submit projects the chosen ones. Internal: the browse step's tree "
                     "drives it.",
      .methods     = {"POST"},
      .hidden      = true,
      .inputSchema = {{"properties",
                       {{"key", {{"type", "string"}, {"description", "ROOT key path of the THnSparse."}}}}}},
  });

  Ndmspc::RegisterMcpTool(std::string(kInternals) + "/project", {
      .description = "Project a THnSparse onto 1-3 axes (use0/use1/...), with an optional per-axis "
                     "min/max and rebin, and draw the result. Internal: the projection dialog's submit "
                     "drives it.",
      .methods     = {"POST"},
      .hidden      = true,
      .inputSchema = {{"properties",
                       {{"key", {{"type", "string"}, {"description", "ROOT key path of the THnSparse."}}},
                        {"drawOpts",
                         {{"type", "string"}, {"description", "jsroot draw options, e.g. 'colz'."}}}}}},
  });

  // ===========================================================================
  //  /api/browser/open — open, report or close the file
  // ===========================================================================
  handlers[group + "/open"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                 std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "browser";
    auto * server  = ctx.Server();
    TFile * file   = OpenFileOf(ctx);

    if (ctx.IsGet()) {
      if (file != nullptr && !file->IsZombie()) {
        httpOut["result"] = "success";
        httpOut["file"]   = file->GetName();
      }
      else {
        ctx.Result("No ROOT file is open");
      }
      return;
    }

    if (ctx.IsPost()) {
      const std::string filename = ctx.GetString("file");
      if (filename.empty()) {
        ctx.Result("Missing 'file' parameter for browser/open");
        return;
      }

      if (file != nullptr && !file->IsZombie() && filename == file->GetName()) {
        // Re-opening the same file just re-publishes its tree.
        PublishOpenSchema(ctx, filename);
        PublishTreeSchema(ctx, BuildTree(file, ExpandedOf(ctx)), "");
        ctx.Success();
        return;
      }

      if (file != nullptr) CloseBrowserFile(ctx);

      TFile * opened = TFile::Open(filename.c_str());
      if (opened == nullptr || opened->IsZombie()) {
        if (opened != nullptr) delete opened;
        NLogError("[browser] Failed to open ROOT file: %s", filename.c_str());
        ctx.Result("Failed to open ROOT file: " + filename);
        return;
      }

      server->AddInputObject(ctx.ObjectName(kFileObject), opened);
      StoreExpanded(ctx, {});

      PublishOpenSchema(ctx, filename);
      PublishTreeSchema(ctx, BuildTree(opened, {}), "");
      ctx.Success();
      return;
    }

    if (ctx.IsDelete()) {
      if (file != nullptr) CloseBrowserFile(ctx);
      StoreExpanded(ctx, {});
      ctx.Success();
      return;
    }

    httpOut["error"] = "Unsupported HTTP method for browser/open";
  };

  // ===========================================================================
  //  /api/browser/browse — the tree step (Run is optional; the tree is live either way)
  // ===========================================================================
  handlers[group + "/browse"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                   std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "browser";
    TFile *        file = RequireOpenFile(ctx);
    if (file == nullptr) return;

    PublishTreeSchema(ctx, BuildTree(file, ExpandedOf(ctx)), "");
    ctx.Success();
  };

  // ===========================================================================
  //  /api/rbrowser/ls — expand a folder and re-publish the tree (internal)
  // ===========================================================================
  handlers[std::string(kInternals) + "/ls"] =
      [](std::string method, json & httpIn, json & httpOut, json & wsOut,
         std::map<std::string, TObject *> & objects) {
        Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
        wsOut["group"] = "browser";
        TFile * file   = RequireOpenFile(ctx);
        if (file == nullptr) return;

        const std::string key = ctx.GetString("key");
        if (key.empty()) {
          ctx.Result("Missing 'key' parameter for browser ls");
          return;
        }

        std::set<std::string> expanded = ExpandedOf(ctx);
        expanded.insert(key);
        StoreExpanded(ctx, expanded);

        PublishTreeSchema(ctx, BuildTree(file, expanded), "");
        ctx.Success();
      };

  // ===========================================================================
  //  /api/rbrowser/draw — draw an object into a pad (internal)
  // ===========================================================================
  handlers[std::string(kInternals) + "/draw"] =
      [](std::string method, json & httpIn, json & httpOut, json & wsOut,
         std::map<std::string, TObject *> & objects) {
        Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
        wsOut["group"] = "browser";
        TFile * file   = RequireOpenFile(ctx);
        if (file == nullptr) return;

        const std::string key = ctx.GetString("key");
        if (key.empty()) {
          ctx.Result("Missing 'key' parameter for browser draw");
          return;
        }

        const std::string drawOpts = ctx.GetString("drawOpts");
        const std::string branch   = ctx.GetString("branch");

        // A branch is drawn as a histogram of it; anything else is drawn as it is. `goff` keeps the
        // projection off any canvas, so the histogram it produces can be serialized on its own.
        TObject *   object = nullptr;
        std::string label  = key;
        if (!branch.empty()) {
          TTree * tree = dynamic_cast<TTree *>(file->Get(key.c_str()));
          if (tree == nullptr) {
            ctx.Result("No tree at key: " + key);
            return;
          }
          tree->Draw(branch.c_str(), "", "goff");
          TH1 * hist = tree->GetHistogram();
          // `Draw` names the projection "htemp"; name it after the branch so the plot says what it is.
          if (hist != nullptr) hist->SetName(branch.c_str());
          object = hist;
          label  = branch;
        }
        else {
          object = file->Get(key.c_str());
          if (object != nullptr) label = StripCycle(object->GetName());
        }

        if (object == nullptr) {
          ctx.Result("No object at key: " + key);
          return;
        }

        // No pad is named: the pad view decides where it lands (its fixed pad, or the rotating ones).
        // `replace`, because browsing draws one object at a time - a click means "show me this", so the
        // pad shows it rather than collecting a tab for every object looked at. A view that keeps a tab
        // per drawing is the default elsewhere; the tool that wants otherwise says so (see NdmspcPadSource).
        ctx.ShowRoot(object, "", label, drawOpts, json::object(), /*replace=*/true);
        if (ctx.HasError()) return;

        // Keep the tree on screen (and the picked node), and remember what was drawn last.
        PublishTreeSchema(ctx, BuildTree(file, ExpandedOf(ctx)), key);
        ctx.Success();
      };

  // ===========================================================================
  //  /api/rbrowser/sparse — open the THnSparse projection dialog (internal)
  // ===========================================================================
  handlers[std::string(kInternals) + "/sparse"] =
      [](std::string method, json & httpIn, json & httpOut, json & wsOut,
         std::map<std::string, TObject *> & objects) {
        Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
        wsOut["group"] = "browser";
        TFile * file   = RequireOpenFile(ctx);
        if (file == nullptr) return;

        const std::string key = ctx.GetString("key");
        if (key.empty()) {
          ctx.Result("Missing 'key' parameter for browser sparse");
          return;
        }

        THnSparse * sparse = dynamic_cast<THnSparse *>(file->Get(key.c_str()));
        if (sparse == nullptr) {
          ctx.Result("No THnSparse at key: " + key);
          return;
        }

        // The dialog lists the axes and projects the chosen ones; its submit runs rbrowser/project. A
        // configuration kept for this axis signature (this object, or another with the same axes) — the
        // axes, the draw options and the SAME choice — is what it opens on.
        ctx.Dialog("Project " + StripCycle(sparse->GetName()),
                   SparseDialogSchema(sparse, SparseSaved(ctx, SparseSignature(sparse))),
                   Ndmspc::NRouteContext::Action(std::string(kInternals) + "/project", "POST",
                                                 json{{"key", key}}),
                   json{{"submit", "Project"}});
        Release(sparse); // only its axes were needed; a sparse is big, so do not keep it
        if (ctx.HasError()) return;
        ctx.Success();
      };

  // ===========================================================================
  //  /api/rbrowser/project — project a THnSparse and draw it (internal)
  // ===========================================================================
  handlers[std::string(kInternals) + "/project"] =
      [](std::string method, json & httpIn, json & httpOut, json & wsOut,
         std::map<std::string, TObject *> & objects) {
        Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
        wsOut["group"] = "browser";
        TFile * file   = RequireOpenFile(ctx);
        if (file == nullptr) return;

        const std::string key = ctx.GetString("key");
        if (key.empty()) {
          ctx.Result("Missing 'key' parameter for browser sparse projection");
          return;
        }

        THnSparse * sparse = dynamic_cast<THnSparse *>(file->Get(key.c_str()));
        if (sparse == nullptr) {
          ctx.Result("No THnSparse at key: " + key);
          return;
        }

        const int ndim = sparse->GetNdimensions();

        // The projection the dialog asked for: its `axes` table has one row per axis, in ascending
        // index order. A row marked `use` is projected (and its rebin is applied after the projection);
        // a row's min/max become a bin range when they narrow the axis — for *every* axis, projected or
        // not, since narrowing a non-projected axis (a pT cut while projecting mass) carves the sample
        // the projection is built from. `reset` restores every axis first, so a row left at full is full.
        const json rows = httpIn.value("axes", json::array());

        std::vector<int>                axes;
        std::map<int, std::vector<int>> ranges;
        std::map<int, int>              rebins;

        for (int i = 0; i < static_cast<int>(rows.size()) && i < ndim; i++) {
          if (!rows[i].is_object()) continue;
          TAxis * axis = sparse->GetAxis(i);
          if (axis == nullptr) continue;

          if (rows[i].value("use", false)) {
            axes.push_back(i);
            const int rebin = rows[i].value("rebin", 1);
            if (rebin > 1) rebins[i] = rebin;
          }

          const double lo = rows[i].value("min", axis->GetXmin());
          const double hi = rows[i].value("max", axis->GetXmax());
          if (!(hi > lo)) continue;
          int binLo = axis->FindBin(lo);
          int binHi = axis->FindBin(hi);
          if (binLo < 1) binLo = 1;
          if (binHi > axis->GetNbins()) binHi = axis->GetNbins();
          if (binHi < binLo) continue;
          ranges[i] = {binLo, binHi};
        }

        if (axes.empty()) {
          ctx.Result("Select at least one axis to project");
          return;
        }
        if (axes.size() > 3) {
          ctx.Result("A projection uses at most three axes");
          return;
        }
        if (!ranges.empty()) Ndmspc::NUtils::SetAxisRanges(sparse, ranges, false, false, true);

        TH1 * projection = Ndmspc::NUtils::ProjectTHnSparse(sparse, axes, "");
        if (projection == nullptr) {
          ctx.Result("Failed to project " + key);
          return;
        }

        // An optional per-axis rebin, on the projected histogram (axis position 0 -> X, 1 -> Y, 2 -> Z).
        // The rebin returns the histogram it produced (the same one, or a new one), so the return is
        // what the rest of the handler uses.
        for (size_t pos = 0; pos < axes.size() && projection != nullptr; pos++) {
          const auto it = rebins.find(axes[pos]);
          if (it == rebins.end()) continue;
          const int factor = it->second;
          // A rebin ROOT cannot do — a variable-width axis — leaves the projection as it was: `Rebin`
          // answers `nullptr` rather than refusing, so the result is taken only when it produced a
          // histogram. Assigning it either way is what used to lose the whole projection.
          TH1 * rebinned = nullptr;
          if (pos == 0) {
            rebinned = projection->RebinX(factor);
          }
          else if (auto * two = dynamic_cast<TH2 *>(projection)) {
            if (pos == 1) rebinned = two->RebinY(factor);
            else if (auto * three = dynamic_cast<TH3 *>(projection)) rebinned = three->RebinZ(factor);
          }
          if (rebinned != nullptr) projection = rebinned;
        }
        if (projection == nullptr) {
          ctx.Result("Failed to rebin " + key);
          return;
        }

        // The projection's name: its own tab, and the histogram's name. Defaults to "projection", so
        // projections share one canvas unless the user names them differently for a tab of their own.
        std::string name = ctx.GetString("name");
        if (name.empty()) name = "projection";

        // The projection's title says what it is, in names: the object it came from and the axes it was
        // made of (`hns mass-pt projection`). Not the object's ROOT title, which is prose written for a
        // reader - and the object is named too, because two objects with the same axes are otherwise
        // two identical titles over one canvas.
        std::string axesLabel;
        for (const auto position : axes) {
          TAxis * axis = sparse->GetAxis(static_cast<int>(position));
          if (axis == nullptr) continue;
          const char * axisName = axis->GetName();
          if (axisName == nullptr || *axisName == '\0') continue;
          if (!axesLabel.empty()) axesLabel += "-";
          axesLabel += axisName;
        }
        const char * objectName = sparse->GetName();
        std::string  title      = (objectName != nullptr && *objectName != '\0')
                                      ? std::string(objectName) + " "
                                      : std::string();
        title += axesLabel.empty() ? "projection" : axesLabel + " projection";
        projection->SetTitle(title.c_str());

        const std::string options = ctx.GetString("drawOpts");
        const bool        same    = httpIn.value("same", false);

        // Keep this configuration under the axes' signature, so the next dialog opens on it - and a
        // different object with the same axes comes up with it too. The name and the SAME choice are
        // kept with it, so both stay as the user left them.
        StoreSparse(ctx, SparseSignature(sparse),
                    json{{"axes", rows}, {"drawOpts", options}, {"same", same}, {"name", name}});

        // The projection is an object of its own now; release the sparse rather than leave a big one per
        // projection in memory (the next projection re-reads it). Everything below uses only `key`.
        Release(sparse);

        // The canvas a projection belongs to is keyed by its name (and the file): one name, one canvas.
        // Within that, a projection is unique by what it is (object, axes, options), which is what makes
        // a replayed step (the server re-runs one to make a combination live) add nothing rather than a
        // twin.
        const std::string overlayKey = std::string(file->GetName()) + "|" + name;
        const std::string signature  = key + "|" + rows.dump() + "|" + options;
        TList *           overlay    = OverlayList(ctx, overlayKey);
        TList *           seen       = OverlaySeen(ctx, overlayKey);
        if (overlay == nullptr || seen == nullptr) {
          ctx.Result("Cannot keep the projections");
          return;
        }
        // Same canvas off starts a new canvas: the previous one is forgotten and this projection is its
        // first, drawn as its own object. Same canvas on adds to the canvas in hand (the first curve
        // draws the axes, every one after it overlays "same") — so the two options together read as
        // "start a canvas" and "draw on top of the last one".
        if (!same) {
          overlay->Clear();
          seen->Clear();
        }

        // The first curve of a canvas is named `name`; the later ones get a suffix, because ROOT's pad
        // replaces a primitive it already holds under the same name — same names would leave one curve.
        const int index = overlay->GetEntries();
        projection->SetName((index == 0 ? name : name + " " + std::to_string(index + 1)).c_str());

        if (seen->FindObject(signature.c_str()) == nullptr) {
          // Cap the canvas: with the cap reached the oldest curve falls off, its signature with it (the
          // two lists are added to in lockstep).
          while (overlay->GetEntries() >= MaxOverlayCurves()) {
            TObject * oldest = overlay->At(0);
            overlay->Remove(oldest);
            delete oldest;
            if (seen->GetEntries() > 0) {
              TObject * oldestSignature = seen->At(0);
              seen->Remove(oldestSignature);
              delete oldestSignature;
            }
          }
          seen->Add(new TObjString(signature.c_str()));
          overlay->Add(projection); // the list owns it from here
        }

        if (same) {
          // ROOT composes the canvas in hand; the pad replaces what it had. The projections live on in
          // the list; the canvas is ours.
          TCanvas * canvas = OverlayCanvas(overlay, options);
          ctx.ShowRoot(canvas, "", name, "", json::object(), /*replace*/ true);
          delete canvas;
        }
        else {
          ctx.ShowRoot(projection, "", name, options);
        }
        if (ctx.HasError()) return;
        ctx.Success();
      };
}
