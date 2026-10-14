#ifndef Ndmspc_NGnHistoryEntry_H
#define Ndmspc_NGnHistoryEntry_H
#include <TNamed.h>
#include "ndmspc/core/NUtils.h"

namespace Ndmspc {

///
/// \class NHistoryEntry
///
/// \brief NHistoryEntry object
///	\author Martin Vala <mvala@cern.ch>
///

class NHistoryEntry : public TNamed {
  public:
  /**
   * @brief Constructor.
   * @param name Object name (default: "").
   * @param title Object title (default: "").
   */
  NHistoryEntry(const char * name = "", const char * title = "");
  virtual ~NHistoryEntry();


  /// @brief Get the input JSON payload.
  json GetPayloadIn() const { return fIn; }
  /// @brief Set the input JSON payload.
  /// @param payload Input payload.
  void SetPayloadIn(const json & payload) { fIn = payload; }
  /// @brief Get the output JSON payload.
  json GetPayloadOut() const { return fOut; }
  /// @brief Set the output JSON payload.
  /// @param payload Output payload.
  void SetPayloadOut(const json & payload) { fOut = payload; }
  /// @brief Get the WebSocket output JSON payload.
  json GetPayloadWsOut() const { return fWsOut; }
  /// @brief Set the WebSocket output JSON payload.
  /// @param payload WebSocket output payload.
  void SetPayloadWsOut(const json & payload) { fWsOut = payload; }

  /// @brief Get the workspace JSON associated with this entry.
  json GetWorkspace() const { return fWorkspace; }
  /// @brief Set the workspace JSON associated with this entry.
  /// @param workspace Workspace JSON.
  void SetWorkspace(const json & workspace) { fWorkspace = workspace; }

  /**
   * @brief The combination node this entry was created for ("" when it is not a node action).
   *
   * A step's own objects and session are keyed by the node it ran on, so removal is by node rather
   * than by route name - two sessions of one group share a route name but not a node.
   */
  const std::string & GetNode() const { return fNode; }
  /// @brief Set the combination node this entry was created for.
  /// @param node Combination node id.
  void SetNode(const std::string & node) { fNode = node; }

  /**
   * @brief The session (root node id) this entry belongs to.
   *
   * What per-session runtime state - the objects a tool creates and the workspace defaults it
   * publishes - is keyed by (see NRouteContext::ObjectName). Recorded here so replaying this
   * entry's DELETE frees the objects of the session it was made in, not whatever session the
   * request that triggered the replay happens to be in.
   */
  const std::string & GetSession() const { return fSession; }
  /// @brief Set the session (root node id) this entry belongs to.
  /// @param session Session root node id.
  void SetSession(const std::string & session) { fSession = session; }

  private:
  json fIn;        ///< Input JSON object
  json fOut;       ///< Output JSON object
  json fWsOut;     ///< Websocket output JSON object
  json fWorkspace; ///< Workspace schema JSON object
  std::string fNode;    ///< Combination node id the entry was created for ("" = none)
  std::string fSession; ///< Root node id of that node's session ("" = none)

  /// \cond CLASSIMP
  ClassDef(NHistoryEntry, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
