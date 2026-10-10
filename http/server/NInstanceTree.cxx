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
  // Nothing to show: fall back to the action's own name, so a node is never blank.
  const auto slash = action.rfind('/');
  return slash == std::string::npos ? action : action.substr(slash + 1);
}

} // namespace

NInstanceTree::NInstanceTree(json & store) : fStore(store) { Ensure(); }

void NInstanceTree::Ensure()
{
  if (!fStore.is_object()) fStore = json::object();
  if (!fStore.contains("next") || !fStore["next"].is_number_integer()) fStore["next"] = 1;
  if (!fStore.contains("active") || !fStore["active"].is_array()) fStore["active"] = json::array();
  if (!fStore.contains("activeGroups") || !fStore["activeGroups"].is_object()) fStore["activeGroups"] = json::object();
  if (!fStore.contains("nodes") || !fStore["nodes"].is_object()) fStore["nodes"] = json::object();
}

void NInstanceTree::Reset()
{
  fStore = json{{"next", 1}, {"active", json::array()}, {"activeGroups", json::object()}, {"nodes", json::object()}};
}

bool NInstanceTree::Empty() const { return fStore["nodes"].empty(); }

std::string NInstanceTree::Create(const std::string & action, const json & params,
                                  const std::string & parent, const std::string & label)
{
  Ensure();

  // The same action with the same inputs, under the same parent, is the same instance: a repeated
  // run reuses the node it already has instead of growing a twin beside it. Running a step again —
  // or opening the same file a second time — therefore keeps one path, and the handler's fresh
  // objects land on that node, which is exactly what re-running is for.
  const json effective = params.is_null() ? json::object() : params;
  for (const std::string & candidate : (parent.empty() ? Roots() : Children(parent))) {
    const json node = Get(candidate);
    if (!node.is_object()) continue;
    if (node.value("action", std::string()) != action) continue;
    if (!label.empty() && node.value("label", std::string()) != label) continue;
    if (node.value("params", json::object()) != effective) continue;
    return candidate;
  }

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

void NInstanceTree::SetParams(const std::string & id, const json & params)
{
  if (!Has(id)) return;
  fStore["nodes"][id]["params"] = params.is_null() ? json::object() : params;
  fStore["nodes"][id]["label"]  = LabelFor(Action(id), fStore["nodes"][id]["params"]);
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

  // The group whose live chain this belongs to, read before the nodes go (the action is gone with them).
  const std::string group = GroupOf(Action(id));

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

  // Drop this group's live-path tail if it pointed into the removed subtree (SetActive keeps the
  // room's own selected path in step with it).
  auto active = Active(group);
  if (std::find(active.begin(), active.end(), id) != active.end()) {
    while (!active.empty() && !Has(active.back())) active.pop_back();
    SetActive(group, active);
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

std::vector<std::string> NInstanceTree::Active(const std::string & group) const
{
  if (group.empty()) return Active();
  std::vector<std::string> active;
  if (!fStore.contains("activeGroups") || !fStore["activeGroups"].is_object()) return active;
  const json held = fStore["activeGroups"].value(group, json::array());
  if (!held.is_array()) return active;
  for (const auto & id : held) active.push_back(id.get<std::string>());
  return active;
}

void NInstanceTree::SetActive(const std::string & group, const std::vector<std::string> & path)
{
  Ensure();
  if (!group.empty()) fStore["activeGroups"][group] = path;
  // The room's own selected path: the one last set, which is what a client shows as selected.
  fStore["active"] = path;
}

void NInstanceTree::SetActive(const std::vector<std::string> & path)
{
  SetActive(path.empty() ? std::string() : GroupOf(Action(path.front())), path);
}

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
  json groups = json::object();
  for (auto it = fStore["nodes"].begin(); it != fStore["nodes"].end(); ++it) {
    const std::string group = GroupOf(it.value().value("action", ""));
    json              node  = it.value();
    node.erase("children"); // rebuildable from parent links
    groups[group]["nodes"][it.key()] = node;
  }

  // A group's roots are its entry points: no parent, or a parent in another group - so restoring the
  // group on its own still yields every combination it holds.
  for (auto it = groups.begin(); it != groups.end(); ++it) {
    json roots = json::array();
    for (auto node = it.value()["nodes"].begin(); node != it.value()["nodes"].end(); ++node) {
      const json        & stored = fStore["nodes"];
      // A root's parent is an empty string or null, so it is read defensively rather than with value().
      const json        & given  = node.value().contains("parent") ? node.value()["parent"] : json();
      const std::string  parent  = given.is_string() ? given.get<std::string>() : "";
      const json         holder  = parent.empty() ? json::object() : stored.value(parent, json::object());
      if (parent.empty() || GroupOf(holder.value("action", "")) != it.key()) roots.push_back(node.key());
    }
    it.value()["roots"] = roots;
  }

  // `active` is the room's own selected path; `activeGroups` is each group's live chain, which is what
  // a restore has to bring back per group.
  return json{{"v", 3},
              {"next", fStore["next"]},
              {"active", Active()},
              {"activeGroups", fStore["activeGroups"]},
              {"groups", groups}};
}

json NInstanceTree::Snapshot(const std::string & group) const
{
  if (group.empty()) return Snapshot();

  // The group's own nodes plus everything below its roots, whichever group those descendants belong
  // to: a combination that leaves its group is still whole.
  json keep = json::object();
  std::function<void(const std::string &)> walk = [&](const std::string & id) {
    if (keep.contains(id) || !Has(id)) return;
    keep[id] = Get(id);
    for (const auto & child : Children(id)) walk(child);
  };
  for (const auto & root : Roots()) {
    if (GroupOf(Action(root)) == group) walk(root);
  }

  const json full   = Snapshot();
  json       groups = json::object();
  for (auto it = full["groups"].begin(); it != full["groups"].end(); ++it) {
    json nodes = json::object();
    for (auto node = it.value()["nodes"].begin(); node != it.value()["nodes"].end(); ++node) {
      if (keep.contains(node.key())) nodes[node.key()] = node.value();
    }
    if (nodes.empty()) continue;
    json roots = json::array();
    for (const auto & root : it.value().value("roots", json::array())) {
      if (keep.contains(root.get<std::string>())) roots.push_back(root);
    }
    groups[it.key()] = json{{"roots", roots}, {"nodes", nodes}};
  }

  return json{{"v", 3},
              {"next", full["next"]},
              {"active", full["active"]},
              {"activeGroups", json{{group, Active(group)}}},
              {"groups", groups}};
}

bool NInstanceTree::HasNodes(const json & snapshot)
{
  if (!snapshot.is_object()) return false;
  if (snapshot.contains("groups") && snapshot["groups"].is_object()) {
    for (auto it = snapshot["groups"].begin(); it != snapshot["groups"].end(); ++it) {
      if (!it.value().value("nodes", json::object()).empty()) return true;
    }
    return false;
  }
  return !snapshot.value("nodes", json::object()).empty(); // version 2
}

void NInstanceTree::Restore(const json & snapshot)
{
  Reset();
  if (!snapshot.is_object()) return;
  if (snapshot.contains("next") && snapshot["next"].is_number_integer()) fStore["next"] = snapshot["next"];

  json nodes = json::object();
  if (snapshot.contains("groups") && snapshot["groups"].is_object()) {
    for (auto it = snapshot["groups"].begin(); it != snapshot["groups"].end(); ++it) {
      const json group = it.value().value("nodes", json::object());
      for (auto node = group.begin(); node != group.end(); ++node) nodes[node.key()] = node.value();
    }
  }
  else if (snapshot.contains("nodes") && snapshot["nodes"].is_object()) {
    nodes = snapshot["nodes"]; // version 2: one flat tree, regrouped by each node's own action
  }
  Adopt(nodes);

  if (snapshot.contains("active") && snapshot["active"].is_array()) fStore["active"] = snapshot["active"];
  if (snapshot.contains("activeGroups") && snapshot["activeGroups"].is_object()) {
    fStore["activeGroups"] = snapshot["activeGroups"];
  }
}

void NInstanceTree::Restore(const json & snapshot, const std::string & group)
{
  if (!snapshot.is_object() || group.empty()) {
    Restore(snapshot);
    return;
  }

  // Whatever the document holds, in either shape, by id.
  json document = json::object();
  if (snapshot.contains("groups") && snapshot["groups"].is_object()) {
    for (auto it = snapshot["groups"].begin(); it != snapshot["groups"].end(); ++it) {
      const json held = it.value().value("nodes", json::object());
      for (auto node = held.begin(); node != held.end(); ++node) document[node.key()] = node.value();
    }
  }
  if (snapshot.contains("nodes") && snapshot["nodes"].is_object()) {
    for (auto it = snapshot["nodes"].begin(); it != snapshot["nodes"].end(); ++it) document[it.key()] = it.value();
  }

  const auto parentOf = [&document](const json & node) {
    const json parent = node.contains("parent") ? node["parent"] : json();
    return parent.is_string() ? parent.get<std::string>() : std::string();
  };

  // The group's own nodes, plus everything below them whichever group it belongs to: a chain is not
  // cut in half by a restore either.
  json taken = json::object();
  std::function<void(const std::string &)> take = [&](const std::string & id) {
    if (taken.contains(id) || !document.contains(id)) return;
    taken[id] = document[id];
    for (auto it = document.begin(); it != document.end(); ++it) {
      if (parentOf(it.value()) == id) take(it.key());
    }
  };
  for (auto it = document.begin(); it != document.end(); ++it) {
    if (GroupOf(it.value().value("action", "")) == group) take(it.key());
  }

  // Ids are per tree (`i1`, `i2`, ...), so the incoming nodes are given fresh ones: a scoped restore
  // merges two trees, and must not overwrite a node that merely shares a number.
  int                                next = fStore.value("next", 1);
  std::map<std::string, std::string> renamed;
  for (auto it = taken.begin(); it != taken.end(); ++it) {
    renamed[it.key()] = "i" + std::to_string(next++);
    it.value()["parent"] = parentOf(it.value()); // rewritten below, through `renamed`
  }

  // Everything outside the group survives, and the group's nodes are replaced by the incoming ones.
  json nodes = json::object();
  for (auto it = fStore["nodes"].begin(); it != fStore["nodes"].end(); ++it) {
    if (GroupOf(it.value().value("action", "")) != group) nodes[it.key()] = it.value();
  }
  for (auto it = taken.begin(); it != taken.end(); ++it) {
    json node      = it.value();
    const auto was = renamed.find(parentOf(node));
    node["parent"] = was == renamed.end() ? parentOf(node) : was->second;
    nodes[renamed.at(it.key())] = node;
  }
  fStore["next"] = next;
  Adopt(nodes);

  // The live paths only survive where they still point at nodes (this group's nodes were replaced).
  std::vector<std::string> kept;
  for (const auto & id : Active(group)) {
    if (Has(id)) kept.push_back(id);
  }
  fStore["activeGroups"][group] = kept;
  json active = json::array();
  for (const auto & id : Active()) {
    if (Has(id)) active.push_back(id);
  }
  fStore["active"] = active;
}

void NInstanceTree::Adopt(const json & nodes)
{
  fStore["nodes"] = json::object();
  for (auto it = nodes.begin(); it != nodes.end(); ++it) {
    json node        = it.value();
    node["children"] = json::array();
    fStore["nodes"][it.key()] = node;
  }
  // Re-link children from the parents.
  for (auto it = fStore["nodes"].begin(); it != fStore["nodes"].end(); ++it) {
    const std::string parent =
        it.value().contains("parent") && it.value()["parent"].is_string() ? it.value()["parent"].get<std::string>() : "";
    if (!parent.empty() && Has(parent)) fStore["nodes"][parent]["children"].push_back(it.key());
  }
}

std::string NInstanceTree::GroupOf(const std::string & action)
{
  // Everything before the key's **last** slash, which is where a key's group ends: the namespace stays
  // part of it (`ndmspc/ngnt` for `ndmspc/ngnt/open`), so a family is not confused with the namespace it
  // lives in and a control tool (`ndmspc/session`) is not taken for a step of the families beside it.
  const auto pos = action.rfind('/');
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
  // What a node is called is the action's own business - a tool says it with a template, or the
  // arguments are guessed. A session's name is the room's, kept apart from this (see SessionList).
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
