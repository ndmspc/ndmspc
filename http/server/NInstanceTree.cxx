#include "NInstanceTree.h"

#include <algorithm>

#include "ndmspc/http/NHttpServer.h" ///< gNdmspcMcpTools / NMcpToolInfo (the dependency template)

namespace Ndmspc {

namespace {

/// @brief One argument as it reads inside a node's label (a string bare, anything else its JSON).
std::string LabelValue(const json & value)
{
  if (value.is_null()) return {};
  if (value.is_string()) return value.get<std::string>();
  return value.dump();
}

/// @brief A label template with each `{{ arg }}` filled from the node's arguments.
std::string RenderLabel(const std::string & tmpl, const json & params)
{
  std::string out;
  std::size_t pos = 0;
  while (pos < tmpl.size()) {
    const std::size_t open = tmpl.find("{{", pos);
    if (open == std::string::npos) {
      out += tmpl.substr(pos);
      break;
    }
    out += tmpl.substr(pos, open - pos);
    const std::size_t close = tmpl.find("}}", open + 2);
    if (close == std::string::npos) {
      out += tmpl.substr(open); // an unterminated placeholder is left as written
      break;
    }
    std::string key = tmpl.substr(open + 2, close - open - 2);
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    if (params.is_object() && params.contains(key)) out += LabelValue(params[key]);
    pos = close + 2;
  }

  // A missing argument leaves a gap, and the space around it: tidy the result so a label never
  // reads "b0 (  )".
  std::string tidy;
  bool        pendingSpace = false;
  for (char c : out) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pendingSpace = !tidy.empty();
      continue;
    }
    if (pendingSpace) {
      tidy += ' ';
      pendingSpace = false;
    }
    tidy += c;
  }
  return tidy;
}

/// @brief The label guessed from the arguments, for an action that declares no template.
std::string GuessedLabel(const std::string & action, const json & params)
{
  if (params.is_object()) {
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (!it.key().empty() && it.key()[0] == '_') continue; // internals (_query, _identity, ...)
      if (it.value().is_string() && !it.value().get<std::string>().empty()) return it.value().get<std::string>();
    }
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (!it.key().empty() && it.key()[0] == '_') continue;
      if (it.value().is_array()) {
        for (const auto & item : it.value()) {
          if (item.is_string() && !item.get<std::string>().empty()) return item.get<std::string>();
        }
      }
    }
  }
  // Nothing to show: fall back to the action's short name, so a node is never blank.
  const auto slash = action.find('/');
  return slash == std::string::npos ? action : action.substr(slash + 1);
}

} // namespace

NInstanceTree::NInstanceTree(json & store) : fStore(store) { Ensure(); }

void NInstanceTree::Ensure()
{
  if (!fStore.is_object()) fStore = json::object();
  if (!fStore.contains("next") || !fStore["next"].is_number_integer()) fStore["next"] = 1;
  if (!fStore.contains("active") || !fStore["active"].is_array()) fStore["active"] = json::array();
  if (!fStore.contains("nodes") || !fStore["nodes"].is_object()) fStore["nodes"] = json::object();
}

void NInstanceTree::Reset()
{
  fStore = json{{"next", 1}, {"active", json::array()}, {"nodes", json::object()}};
}

bool NInstanceTree::Empty() const { return fStore["nodes"].empty(); }

std::string NInstanceTree::Create(const std::string & action, const json & params,
                                  const std::string & parent, const std::string & label)
{
  Ensure();

  const int next = fStore["next"].get<int>();
  fStore["next"] = next + 1;
  const std::string id = "i" + std::to_string(next);

  json node;
  node["action"]   = action;
  node["parent"]   = parent.empty() ? json(nullptr) : json(parent);
  node["params"]   = params.is_null() ? json::object() : params;
  node["children"] = json::array();
  node["label"]    = label.empty() ? LabelFor(action, node["params"]) : label;
  fStore["nodes"][id] = std::move(node);

  if (!parent.empty() && Has(parent)) {
    fStore["nodes"][parent]["children"].push_back(id);
  }
  return id;
}

bool NInstanceTree::Has(const std::string & id) const
{
  return !id.empty() && fStore.contains("nodes") && fStore["nodes"].contains(id);
}

json NInstanceTree::Get(const std::string & id) const { return Has(id) ? fStore["nodes"][id] : json(); }

std::string NInstanceTree::Action(const std::string & id) const
{
  const json node = Get(id);
  return node.is_object() && node.contains("action") ? node["action"].get<std::string>() : "";
}

std::string NInstanceTree::Parent(const std::string & id) const
{
  const json node = Get(id);
  if (!node.is_object() || !node.contains("parent") || node["parent"].is_null()) return "";
  return node["parent"].get<std::string>();
}

json NInstanceTree::Params(const std::string & id) const
{
  const json node = Get(id);
  return node.is_object() && node.contains("params") ? node["params"] : json();
}

json NInstanceTree::State(const std::string & id) const
{
  const json node = Get(id);
  return node.is_object() && node.contains("state") ? node["state"] : json();
}

void NInstanceTree::SetState(const std::string & id, const json & state)
{
  if (Has(id)) fStore["nodes"][id]["state"] = state;
}

std::vector<std::string> NInstanceTree::Children(const std::string & id) const
{
  std::vector<std::string> children;
  const json               node = Get(id);
  if (node.is_object() && node.contains("children") && node["children"].is_array()) {
    for (const auto & child : node["children"]) children.push_back(child.get<std::string>());
  }
  return children;
}

std::vector<std::string> NInstanceTree::Roots() const
{
  std::vector<std::string> roots;
  if (!fStore.contains("nodes")) return roots;
  for (auto it = fStore["nodes"].begin(); it != fStore["nodes"].end(); ++it) {
    // Preserve creation order: ids are "i<N>" and the count is small, so order by N.
    roots.push_back(it.key());
  }
  std::sort(roots.begin(), roots.end(), [](const std::string & a, const std::string & b) {
    return std::stoi(a.substr(1)) < std::stoi(b.substr(1));
  });
  std::vector<std::string> onlyRoots;
  for (const auto & id : roots) {
    if (Parent(id).empty()) onlyRoots.push_back(id);
  }
  return onlyRoots;
}

std::vector<std::string> NInstanceTree::Path(const std::string & id) const
{
  std::vector<std::string> path;
  std::string              cursor = id;
  std::string              guard;
  while (!cursor.empty() && Has(cursor) && cursor != guard) {
    path.push_back(cursor);
    guard  = cursor;
    cursor = Parent(cursor);
  }
  std::reverse(path.begin(), path.end());
  return path;
}

bool NInstanceTree::RemoveSubtree(const std::string & id,
                                  const std::function<void(const std::string &, const std::string &)> & onNode)
{
  if (!Has(id)) return false;

  // Collect the subtree (pre-order) so the caller's teardown runs deepest-first.
  std::vector<std::string> order;
  std::vector<std::string> stack{id};
  while (!stack.empty()) {
    const std::string current = stack.back();
    stack.pop_back();
    order.push_back(current);
    for (const auto & child : Children(current)) stack.push_back(child);
  }
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (onNode) onNode(*it, Action(*it));
  }

  const std::string parent = Parent(id);
  if (!parent.empty() && Has(parent)) {
    auto & siblings = fStore["nodes"][parent]["children"];
    for (auto it = siblings.begin(); it != siblings.end(); ++it) {
      if (it->get<std::string>() == id) {
        siblings.erase(it);
        break;
      }
    }
  }
  for (const auto & node : order) fStore["nodes"].erase(node);

  // Drop the active path's tail if it pointed into the removed subtree.
  auto active = Active();
  if (std::find(active.begin(), active.end(), id) != active.end()) {
    while (!active.empty() && !Has(active.back())) active.pop_back();
    SetActive(active);
  }
  return true;
}

std::vector<std::string> NInstanceTree::Active() const
{
  std::vector<std::string> active;
  if (!fStore.contains("active") || !fStore["active"].is_array()) return active;
  for (const auto & id : fStore["active"]) active.push_back(id.get<std::string>());
  return active;
}

void NInstanceTree::SetActive(const std::vector<std::string> & path) { fStore["active"] = path; }

json NInstanceTree::ToTree() const
{
  std::function<json(const std::string &)> build = [&](const std::string & id) {
    const json node = Get(id);
    json       out;
    out["id"]      = id;
    out["action"]  = node.value("action", "");
    out["label"]   = node.value("label", "");
    if (node.contains("params")) out["params"] = node["params"];
    if (node.contains("state") && !node["state"].is_null()) out["state"] = node["state"];
    out["children"] = json::array();
    for (const auto & child : Children(id)) out["children"].push_back(build(child));
    return out;
  };

  json roots = json::array();
  for (const auto & root : Roots()) roots.push_back(build(root));
  return json{{"active", Active()}, {"roots", roots}};
}

json NInstanceTree::Snapshot() const
{
  json nodes = json::object();
  for (auto it = fStore["nodes"].begin(); it != fStore["nodes"].end(); ++it) {
    json node = it.value();
    node.erase("children"); // rebuildable from parent links
    nodes[it.key()] = node;
  }
  return json{{"v", 2}, {"next", fStore["next"]}, {"active", Active()}, {"nodes", nodes}};
}

void NInstanceTree::Restore(const json & snapshot)
{
  Reset();
  if (!snapshot.is_object()) return;
  if (snapshot.contains("next") && snapshot["next"].is_number_integer()) fStore["next"] = snapshot["next"];
  if (snapshot.contains("nodes") && snapshot["nodes"].is_object()) {
    fStore["nodes"] = json::object();
    for (auto it = snapshot["nodes"].begin(); it != snapshot["nodes"].end(); ++it) {
      json node      = it.value();
      node["children"] = json::array();
      fStore["nodes"][it.key()] = node;
    }
    // Re-link children from the parents.
    for (auto it = snapshot["nodes"].begin(); it != snapshot["nodes"].end(); ++it) {
      const std::string parent =
          it.value().contains("parent") && !it.value()["parent"].is_null() ? it.value()["parent"].get<std::string>() : "";
      if (!parent.empty() && Has(parent)) fStore["nodes"][parent]["children"].push_back(it.key());
    }
  }
  if (snapshot.contains("active") && snapshot["active"].is_array()) fStore["active"] = snapshot["active"];
}

std::string NInstanceTree::GroupOf(const std::string & action)
{
  const auto pos = action.find('/');
  return pos == std::string::npos ? std::string() : action.substr(0, pos);
}

bool NInstanceTree::IsNodeAction(const std::string & action)
{
  if (gNdmspcMcpTools == nullptr) return false;
  const std::string group = GroupOf(action);
  if (group.empty()) return false;
  for (const auto & entry : *gNdmspcMcpTools) {
    if (GroupOf(entry.first) != group) continue;
    if (!entry.second.dependsOn.empty()) return true;
  }
  return false;
}

std::string NInstanceTree::ParentActionFor(const std::string & action)
{
  if (gNdmspcMcpTools == nullptr) return "";
  const auto info = gNdmspcMcpTools->find(action);
  if (info == gNdmspcMcpTools->end()) return "";
  for (const auto & dep : info->second.dependsOn) {
    if (!dep.empty()) return dep;
  }
  return "";
}

std::string NInstanceTree::LabelFor(const std::string & action, const json & params)
{
  // A macro can say what a node is called, e.g. "{{ binningName }} ({{ levels }})".
  if (gNdmspcMcpTools != nullptr) {
    const auto info = gNdmspcMcpTools->find(action);
    if (info != gNdmspcMcpTools->end() && !info->second.label.empty()) {
      const std::string rendered = RenderLabel(info->second.label, params);
      if (!rendered.empty()) return rendered;
    }
  }
  return GuessedLabel(action, params);
}

} // namespace Ndmspc
