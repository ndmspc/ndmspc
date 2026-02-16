
#include "NGnHistory.h"
#include "NGnHttpServer.h"


#include "NLogger.h"
#include <algorithm>
#include <fstream>

namespace Ndmspc {

NGnHistory::NGnHistory(const char* name, const char* title, NGnHttpServer * server)
  : TNamed(name, title), fServer(server) {}

NGnHistory::~NGnHistory() {
  Clear();
}

void NGnHistory::Print(Option_t * option) const {
  (void)option;
  NLogInfo("NGnHistory with %zu entries:", fEntries.size());
  for (size_t i = 0; i < fEntries.size(); i++) {
    NLogInfo("  [%zu]: %s payload: in=%s out=%s wsOut=%s", i, fEntries[i]->GetName(),
             fEntries[i]->GetPayloadIn().dump().c_str(), fEntries[i]->GetPayloadOut().dump().c_str(),
             fEntries[i]->GetPayloadWsOut().dump().c_str());
  }
  NLogInfo("Workspace: %s", GetWorkspace().dump().c_str());
}

void NGnHistory::AddEntry(NGnHistoryEntry* entry) {
  if (entry) {
    RemoveEntry(entry->GetName());
    NLogTrace("Adding history entry: %s", entry->GetName());
    NLogTrace("Config: %s", entry->GetPayloadIn().dump().c_str());
    fEntries.push_back(entry);
  }
}

bool NGnHistory::RemoveEntry(int index) {
  if (index < 0 || index >= static_cast<int>(fEntries.size())) {
    NLogError("Invalid history entry index: %d", index);
    return false;
  }

  NGnHistoryEntry * entry = fEntries.at(index);
  json              in    = entry->GetPayloadIn();
  json              out;
  json              wsOut;
  NLogTrace("Removing history entry: %s", entry->GetName());
  NLogTrace("Config: %s", in.dump().c_str());
  NLogTrace("Invoking HTTP handler for DELETE on entry: %s", entry->GetName());
  fServer->GetHttpHandlers()[entry->GetName()]("DELETE", in, out, wsOut, fServer->GetObjectsMap());
  delete entry;

  fEntries.erase(fEntries.begin() + index);
  return true;

}

bool NGnHistory::RemoveEntry(const std::string& name) {
  // Find if entry exists and remove it along with all newer entries
  bool found = false;
  for (int i = static_cast<int>(fEntries.size()) - 1; i >= 0; i--) {
    NLogTrace("Checking history entry at index %d: %s", i, fEntries.at(i)->GetName());
    std::string existingName = fEntries.at(i)->GetName();
    if (!existingName.compare(name)) {
      NLogTrace("Found existing history entry with same name: %s at index %d, removing newer entries.", name.c_str(), i);
      // remove all entries above it
      int j = -1;
      for (j = fEntries.size() - 1; j > static_cast<int>(i); j--) {
        NLogTrace("Removing history entry at index %zu: %s", j, fEntries.at(j)->GetName());
        RemoveEntry(j);
      }
      if (j >= 0) RemoveEntry(j);
      found = true;
      break;
    }
  }
  return found;
}

void NGnHistory::Clear(Option_t *option) {
  (void)option;
  for (auto* entry : fEntries) delete entry;
  fEntries.clear();
}

bool NGnHistory::LoadFromFile(const std::string& filename) {
  (void)filename;
  // Implement loading logic as needed
  return false;
}

bool NGnHistory::ExportToFile(const std::string& filename) const {
  (void)filename;
  // Implement export logic as needed
  return false;
}

} // namespace Ndmspc

ClassImp(Ndmspc::NGnHistory);