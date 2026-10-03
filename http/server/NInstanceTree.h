#ifndef Ndmspc_NInstanceTree_H
#define Ndmspc_NInstanceTree_H

#include <functional>
#include <string>
#include <vector>

#include "ndmspc/core/NLogger.h" ///< provides the global `json` (nlohmann) alias

namespace Ndmspc {

///
/// \class NInstanceTree
///
/// \brief The tree of created action instances that make up a session.
///
/// The `dependsOn` metadata of a tool group (`NMcpToolInfo`) is a *template* - which action may
/// stand on which. This is the matching *catalogue* of what a client actually created: one node per
/// created action, each naming its parent, so several opens, several reshapes under one open, and
/// so on, can coexist. A **combination** is a path of nodes from a root down to a node.
///
/// The nodes are held as JSON inside the workspace (`NWorkspace::GetCombinations()`), so this is a
/// plain helper over a `json &`, not a ROOT class and not owned by the server. Only the params and
/// the per-node state live here; the live objects of the *active* combination remain the server's
/// own (one `NGnTree`, one navigator), materialized on demand and described by the workspace.
///
/// Node JSON shape:
/// \code
/// { "action":"ngnt/reshape", "parent":"i1", "params":{...}, "children":["i3"], "label":"b",
///   "state":{"point":[0,1]} }
/// \endcode
///
class NInstanceTree {

  public:
  /**
   * @brief Wrap a workspace's combinations JSON.
   * @param store The JSON object to operate on (created empty if it is not an object).
   */
  explicit NInstanceTree(json & store);

  /// @brief Forget every node and reset the id counter.
  void Reset();

  /// @brief Whether there is no node at all.
  bool Empty() const;

  /**
   * @brief Create a node under a parent, or hand back the one that is already the same.
   * @param action The action's handler key (e.g. "ngnt/reshape").
   * @param params The request input to replay the action with (internals already stripped).
   * @param parent The parent node id, or "" for a root.
   * @param label A display label (see LabelFor when empty); a non-empty one is part of the match.
   * @return The node's id — an existing node's when this action, with these parameters and this
   *         label, is already under this parent: a repeated run does not grow a twin beside it.
   */
  std::string Create(const std::string & action, const json & params, const std::string & parent,
                     const std::string & label);

  /// @brief Whether a node exists.
  bool        Has(const std::string & id) const;
  /// @brief A node's JSON, or a null json when it does not exist.
  json        Get(const std::string & id) const;
  /// @brief A node's action, or "" when it does not exist.
  std::string Action(const std::string & id) const;
  /// @brief A node's parent id, or "" for a root / unknown id.
  std::string Parent(const std::string & id) const;
  /// @brief A node's stored params (a null json when unknown).
  json        Params(const std::string & id) const;
  /// @brief A node's stored state (a null json when none).
  json        State(const std::string & id) const;
  /// @brief Store a node's state (e.g. its drill-down point).
  void        SetState(const std::string & id, const json & state);
  /**
   * @brief Give a node new arguments, and the label they derive.
   *
   * A node's arguments are what it is, so this is how a session that was started with nothing takes the
   * arguments of its first step (see the session `new` action): the node keeps its id, so whatever
   * refers to it - the room's active session, a pad's path - goes on doing so.
   */
  void        SetParams(const std::string & id, const json & params);
  /// @brief A node's child ids, in creation order.
  std::vector<std::string> Children(const std::string & id) const;
  /// @brief The root node ids, in creation order.
  std::vector<std::string> Roots() const;
  /// @brief The path of ids from a root down to (and including) a node.
  std::vector<std::string> Path(const std::string & id) const;

  /**
   * @brief Remove a node and everything below it.
   * @param id The subtree's root.
   * @param onNode Called once per removed node, deepest first, with (id, action) - so the caller
   *        can tear down the objects that node owns.
   * @return True when the node existed.
   */
  bool RemoveSubtree(const std::string & id,
                     const std::function<void(const std::string &, const std::string &)> & onNode);

  /// @brief The active combination: the ids from a root to the live node.
  std::vector<std::string> Active() const;
  /**
   * @brief The live combination of one group.
   *
   * Each group has its own live chain: materializing one group's combination must not disturb
   * another's - a browser's open file stays open while the analysis group works, and the other way
   * round. `Active()` is the room's own selected path (the last one set).
   *
   * @param group The group prefix ("ngnt", "browser"); empty gives the room's own path.
   */
  std::vector<std::string> Active(const std::string & group) const;
  /// @brief Set the active combination.
  void                     SetActive(const std::vector<std::string> & path);
  /**
   * @brief Set one group's live combination (and the room's own selected path with it).
   * @param group The group prefix; empty sets the room's own path only.
   * @param path The ids from the group's root to its live node ({} when it has none).
   */
  void                     SetActive(const std::string & group, const std::vector<std::string> & path);

  /// @brief The tree for a client: `{active:[...], roots:[{id,action,label,params,children,state}]}`.
  json ToTree() const;
  /**
   * @brief A flat, replayable snapshot, **keyed by tool group**.
   *
   * \code
   * { "v":3, "next":12, "active":["i3","i4"],
   *   "groups": { "ngnt":   { "roots":["i1"], "nodes":{"i1":{...}} },
   *               "schema": { "roots":["i7"], "nodes":{"i7":{...}} } } }
   * \endcode
   *
   * A node sits under the group of its **own** action, so the group is the key a session is saved and
   * restored by. A group's `roots` are its entry points: its nodes with no parent, plus any of its
   * nodes whose parent belongs to another group - a chain that leaves its group stays restorable on
   * its own. `active` stays one path (a live combination lies within one group).
   *
   * A version 2 snapshot (`{v:2, ..., nodes}`) is still accepted by Restore and regrouped there, so
   * a session stored before this change keeps working; the version is not a migration.
   */
  json Snapshot() const;
  /**
   * @brief A standalone snapshot of one group: its nodes **and** everything below its roots, whichever
   *        group those descendants belong to, so the chain stays whole.
   * @param group The group prefix ("ngnt"); an empty one means the whole snapshot.
   */
  json Snapshot(const std::string & group) const;
  /**
   * @brief Replace the tree from a snapshot.
   * @param snapshot A `{v:3, ...}` snapshot, or a `{v:2, ...}` one (regrouped here).
   */
  void Restore(const json & snapshot);
  /**
   * @brief Replace one group's nodes from a snapshot, leaving every other group alone.
   * @param snapshot A `{v:3, ...}` or `{v:2, ...}` snapshot.
   * @param group The group to take from it; an empty one means the whole snapshot.
   */
  void Restore(const json & snapshot, const std::string & group);
  /// @brief Whether a snapshot holds any node at all, in either version (see the guards that use it).
  static bool HasNodes(const json & snapshot);

  /// @brief The group prefix of an action key ("ngnt/reshape" -> "ngnt").
  static std::string GroupOf(const std::string & action);
  /**
   * @brief Whether an action participates in a combination tree.
   *
   * True when the action's group declares any `dependsOn`: such a group is a pipeline whose
   * instances are nodes. `health`, `state`, the room actions and every macro that declares no
   * dependency keep the plain, single-session behaviour.
   */
  static bool IsNodeAction(const std::string & action);
  /// @brief The one action this action must sit under (its first dependency), or "".
  static std::string ParentActionFor(const std::string & action);
  /// @brief A display label for a node: the first non-empty string param, else "".
  static std::string LabelFor(const std::string & action, const json & params);

  private:
  /// @brief Ensure the store is an object with the expected keys.
  void Ensure();

  /**
   * @brief Install a flat `{id: node}` map as the store's nodes, re-linking children from parents.
   * @param nodes Nodes as the snapshot holds them (children are rebuilt here, so they may be absent).
   */
  void Adopt(const json & nodes);

  json & fStore; ///< The combinations JSON (owned by the workspace).
};

} // namespace Ndmspc
#endif
