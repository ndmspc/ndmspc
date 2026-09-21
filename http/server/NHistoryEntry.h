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


  private:
  json fIn;        ///< Input JSON object
  json fOut;       ///< Output JSON object
  json fWsOut;     ///< Websocket output JSON object
  json fWorkspace; ///< Workspace schema JSON object

  /// \cond CLASSIMP
  ClassDef(NHistoryEntry, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
