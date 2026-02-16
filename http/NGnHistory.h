#ifndef Ndmspc_NGnHistory_H
#define Ndmspc_NGnHistory_H

#include <TNamed.h>
#include <vector>
#include <string>
#include "NGnHistoryEntry.h"

namespace Ndmspc {

///
/// \class NGnHistory
/// \brief Encapsulates history entries for NGnHttpServer
///
class NGnHttpServer;
class NGnHistory : public TNamed {
public:
  NGnHistory(const char* name = "NGnHistory", const char* title = "NGnHistory object", NGnHttpServer * server = nullptr);
  virtual ~NGnHistory();

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

  void SetServer(NGnHttpServer * server) { fServer = server; }  

private:
  json fWorkspace; ///< Workspace schema JSON object
  std::vector<NGnHistoryEntry*> fEntries; ///< History entries
  NGnHttpServer * fServer{nullptr}; ///< Pointer to the HTTP server for invoking handlers

  ClassDefOverride(NGnHistory, 1);
};

} // namespace Ndmspc

#endif
