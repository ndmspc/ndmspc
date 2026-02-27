#ifndef Ndmspc_NGnWorkspace_H
#define Ndmspc_NGnWorkspace_H

#include <TNamed.h>
#include <vector>
#include <string>
#include "NGnHistoryEntry.h"

namespace Ndmspc {

///
/// \class NGnWorkspace
/// \brief Encapsulates workspace entries for NGnHttpServer
///
class NGnHttpServer;
class NGnWorkspace : public TNamed {
public:
  NGnWorkspace(const char* name = "NGnWorkspace", const char* title = "NGnWorkspace object", NGnHttpServer * server = nullptr);
  virtual ~NGnWorkspace();

  virtual void Print(Option_t * option = "") const override;

  void AddEntry(NGnHistoryEntry* entry);
  bool RemoveEntry(int index);
  bool RemoveEntry(const std::string& name);
  void Clear(Option_t *option = "") override;

  bool LoadFromFile(const std::string& filename);
  bool ExportToFile(const std::string& filename) const;

  const std::vector<NGnHistoryEntry*>& GetEntries() const { return fEntries; }
  json& GetWorkspace() { return fWorkspace; }
  const json& GetWorkspace() const { return fWorkspace; }
  json& GetState() { return fState; }
  const json& GetState() const { return fState; }

  void SetServer(NGnHttpServer * server) { fServer = server; }  

private:
  json fWorkspace; ///< Workspace schema JSON object
  json fState;     ///< Additional state information for the workspace
  std::vector<NGnHistoryEntry*> fEntries; ///< Workspace entries
  NGnHttpServer * fServer{nullptr}; ///< Pointer to the HTTP server for invoking handlers

  ClassDefOverride(NGnWorkspace, 1);
};

} // namespace Ndmspc

#endif
