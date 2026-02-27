
#include "NGnWorkspace.h"
#include "NGnHttpServer.h"

#include "NLogger.h"
#include <algorithm>
#include <fstream>


namespace Ndmspc {

NGnWorkspace::NGnWorkspace(const char * name, const char * title, NGnHttpServer * server)
    : TNamed(name, title), fServer(server)
{
}

NGnWorkspace::~NGnWorkspace()
{
  Clear();
}

void NGnWorkspace::Print(Option_t * option) const
{
  (void)option;
  NLogInfo("NGnWorkspace with %zu entries:", fEntries.size());
  for (size_t i = 0; i < fEntries.size(); i++) {
    NLogInfo("  [%zu]: %s payload: in=%s out=%s", i, fEntries[i]->GetName(), fEntries[i]->GetPayloadIn().dump().c_str(),
             fEntries[i]->GetPayloadOut().dump().c_str());
  }
  NLogInfo("Workspace: %s", GetWorkspace().dump().c_str());
  NLogInfo("State: %s", GetState().dump().c_str());
}

void NGnWorkspace::AddEntry(NGnHistoryEntry * entry)
{
  if (entry) {
    RemoveEntry(entry->GetName());
    NLogTrace("Adding workspace entry: %s", entry->GetName());
    NLogTrace("Config: %s", entry->GetPayloadIn().dump().c_str());
    fEntries.push_back(entry);
  }
}

bool NGnWorkspace::RemoveEntry(int index)
{
  if (index < 0 || index >= static_cast<int>(fEntries.size())) {
    NLogError("Invalid workspace entry index: %d", index);
    return false;
  }

  NGnHistoryEntry * entry = fEntries.at(index);
  json              in    = entry->GetPayloadIn();
  json              out;
  json              wsOut;
  NLogTrace("Removing workspace entry: %s", entry->GetName());
  NLogTrace("Config: %s", in.dump().c_str());
  NLogTrace("Invoking HTTP handler for DELETE on entry: %s", entry->GetName());
  fServer->GetHttpHandlers()[entry->GetName()]("DELETE", in, out, wsOut, fServer->GetObjectsMap());

  // if (fWorkspace.contains(entry->GetName())) fWorkspace.erase(entry->GetName());
  delete entry;
  fEntries.erase(fEntries.begin() + index);
  return true;
}

bool NGnWorkspace::RemoveEntry(const std::string & name)
{
  // Find if entry exists and remove it along with all newer entries
  bool found = false;
  for (int i = static_cast<int>(fEntries.size()) - 1; i >= 0; i--) {
    NLogTrace("Checking workspace entry at index %d: %s", i, fEntries.at(i)->GetName());
    std::string existingName = fEntries.at(i)->GetName();
    if (!existingName.compare(name)) {
      NLogTrace("Found existing workspace entry with same name: %s at index %d, removing newer entries.", name.c_str(),
                i);
      // remove all entries above it
      int j = -1;
      for (j = fEntries.size() - 1; j > static_cast<int>(i); j--) {
        NLogTrace("Removing workspace entry at index %zu: %s", j, fEntries.at(j)->GetName());
        RemoveEntry(j);
      }
      if (j >= 0) RemoveEntry(j);
      found = true;
      break;
    }
  }
  return found;
}

void NGnWorkspace::Clear(Option_t * option)
{
  (void)option;
  for (auto * entry : fEntries) {
    if (entry == nullptr) continue;
    if (fWorkspace.contains(entry->GetName())) fWorkspace.erase(entry->GetName());
    delete entry;
  }
  fEntries.clear();
}

bool NGnWorkspace::LoadFromFile(const std::string & filename)
{
  (void)filename;
  // Implement loading logic as needed
  return false;
}

bool NGnWorkspace::ExportToFile(const std::string & filename) const
{
  (void)filename;
  // Implement export logic as needed
  return false;
}

} // namespace Ndmspc

ClassImp(Ndmspc::NGnWorkspace);