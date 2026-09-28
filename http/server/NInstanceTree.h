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
   * @brief Create a node under a parent.
   * @param action The action's handler key (e.g. "ngnt/reshape").
   * @param params The request input to replay the action with (internals already stripped).
   * @param parent The parent node id, or "" for a root.
   * @param label A display label (see LabelFor when empty).
   * @return The new node's id.
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
  /// @brief Set the active combination.
  void                     SetActive(const std::vector<std::string> & path);

  /// @brief The tree for a client: `{active:[...], roots:[{id,action,label,params,children,state}]}`.
  json ToTree() const;
  /// @brief A flat, replayable snapshot: `{v:2, next, active, nodes:{...}}`.
  json Snapshot() const;
  /**
   * @brief Replace the tree from a snapshot.
   * @param snapshot A `{v:2,...}` snapshot (older snapshots are converted by the caller).
   */
  void Restore(const json & snapshot);

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

  json & fStore; ///< The combinations JSON (owned by the workspace).
};

} // namespace Ndmspc
#endif
