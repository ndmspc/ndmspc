///
/// toolBrowser.C — a TBrowser-like ROOT file browser, as a tool group.
///
/// Registers: browser/open, browser/browse (the combination) and the internal rbrowser/ls,
/// rbrowser/draw (hidden helpers the tree drives).
///
/// URLs:  /api/browser/open, /api/browser/browse, /api/rbrowser/ls, /api/rbrowser/draw
///
/// The tool does everything the browser needs: it opens a ROOT file, walks its keys and directories to
/// build the tree, and draws a chosen object into a pad. The visible part is a **combination** —
/// `browse` depends on `open` — so the Explorer shows the group with `open` as the action that starts
/// it and, under it, the `browse` step:
///
///   open ──▶ browse (the file tree)  ──expand──▶ rbrowser/ls   ──click──▶ rbrowser/draw ──▶ a jsroot pad
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
/// `TDavixFile`/`TCurlFile`), which needs the matching ROOT net package installed (`root-net-davix` on
/// Fedora, alongside `root-net-http`); without it ROOT's `TFile::Open` fails on a URL and the tool
/// reports that as an open failure.
///
/// Usage:
///   ndmspc-server -m "macros/tools/toolNgnt.C,macros/tools/toolBrowser.C"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <TBranch.h>
#include <TClass.h>
#include <TDirectory.h>
#include <TFile.h>
#include <TH1.h>
#include <TKey.h>
#include <TList.h>
#include <TObject.h>
#include <TObjArray.h>
#include <TTree.h>

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
  return ctx.GetObject<TFile>(kFileObject);
}

/// Drop the opened file (the server deletes it).
void CloseBrowserFile(Ndmspc::NRouteContext & ctx)
{
  if (auto * server = ctx.Server()) server->RemoveInputObject(kFileObject);
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

      server->AddInputObject(kFileObject, opened);
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
        ctx.ShowRoot(object, "", label, drawOpts);
        if (ctx.HasError()) return;

        // Keep the tree on screen (and the picked node), and remember what was drawn last.
        PublishTreeSchema(ctx, BuildTree(file, ExpandedOf(ctx)), key);
        ctx.Success();
      };
}
