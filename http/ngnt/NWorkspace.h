#ifndef Ndmspc_NGnWorkspace_H
#define Ndmspc_NGnWorkspace_H

#include <TNamed.h>
#include <vector>
#include <string>
#include "NHistoryEntry.h"

namespace Ndmspc {

///
/// \class NWorkspace
/// \brief Encapsulates workspace entries for NHttpServer
///
class NHttpServer;
class NWorkspace : public TNamed {
public:
  /**
   * @brief Constructor.
   * @param name Object name (default: "NWorkspace").
   * @param title Object title (default: "NWorkspace object").
   * @param server Owning HTTP server, used to invoke handlers (default: nullptr).
   */
  NWorkspace(const char* name = "NWorkspace", const char* title = "NWorkspace object", NHttpServer * server = nullptr);
  virtual ~NWorkspace();

  /**
   * @brief Print the workspace entries.
   * @param option Optional ROOT option string (unused).
   */
  virtual void Print(Option_t * option = "") const override;

  /**
   * @brief Append a history entry to the workspace.
   * @param entry Entry to add (ownership is taken by the workspace).
   */
  void AddEntry(NHistoryEntry* entry);
  /**
   * @brief Remove the entry at the given index.
   * @param index Index of the entry to remove.
   * @return True when an entry was removed.
   */
  bool RemoveEntry(int index);
  /**
   * @brief Remove the entry with the given name.
   * @param name Name of the entry to remove.
   * @return True when an entry was removed.
   */
  bool RemoveEntry(const std::string& name);
  /**
   * @brief Remove all entries and reset the workspace schema/state.
   * @param option Optional ROOT option string (unused).
   */
  void Clear(Option_t *option = "") override;

  /**
   * @brief Load the workspace contents from a file.
   * @param filename Input file path.
   * @return True on success.
   */
  bool LoadFromFile(const std::string& filename);
  /**
   * @brief Export the workspace contents to a file.
   * @param filename Output file path.
   * @return True on success.
   */
  bool ExportToFile(const std::string& filename) const;

  /// @brief Get the workspace history entries.
  const std::vector<NHistoryEntry*>& GetEntries() const { return fEntries; }
  /// @brief Get the mutable workspace schema JSON.
  json& GetWorkspace() { return fWorkspace; }
  /// @brief Get the read-only workspace schema JSON.
  const json& GetWorkspace() const { return fWorkspace; }
  /// @brief Get the mutable workspace state JSON.
  json& GetState() { return fState; }
  /// @brief Get the read-only workspace state JSON.
  const json& GetState() const { return fState; }

  /// @brief Get an array of {key, schema} objects in fEntries order for the inspector.
  json GetInspectorEntries() const;

  /**
   * @brief Get the combined inspector schema.
   *
   * Contains title, inspector (group, properties, history) and metadata (workspace
   * state). This merges the history, workspace schema and state for UI consumption.
   *
   * @return The combined inspector schema JSON.
   */
  json GetInspectorSchema() const;

  /**
   * @brief Set the owning HTTP server used to invoke handlers.
   * @param server Pointer to the server (may be null).
   */
  void SetServer(NHttpServer * server) { fServer = server; }  

private:
  json fWorkspace{}; ///< Workspace schema JSON object
  json fState{};     ///< Additional state information for the workspace
  std::vector<NHistoryEntry*> fEntries; ///< Workspace entries
  NHttpServer * fServer{nullptr}; ///< Pointer to the HTTP server for invoking handlers

  /// \cond CLASSIMP
  ClassDefOverride(NWorkspace, 1);
  /// \endcond
};

} // namespace Ndmspc

#endif
