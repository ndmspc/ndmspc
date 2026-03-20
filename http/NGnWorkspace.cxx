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

  // Remove any workspace JSON keys that have no corresponding history entry
  // (e.g. schema previews added by a handler that was just removed)
  std::vector<std::string> orphanedKeys;
  for (auto it = fWorkspace.begin(); it != fWorkspace.end(); ++it) {
    bool hasEntry = false;
    for (const auto & e : fEntries) {
      if (it.key() == std::string(e->GetName())) {
        hasEntry = true;
        break;
      }
    }
    if (!hasEntry) orphanedKeys.push_back(it.key());
  }
  // for (const auto & key : orphanedKeys) {
  //   NLogTrace("Removing orphaned workspace key: %s", key.c_str());
  //   fWorkspace.erase(key);
  // }

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

void NGnWorkspace::Clear(Option_t *)
{
  for (int i = static_cast<int>(fEntries.size()) - 1; i >= 0; i--) {
    RemoveEntry(i);
  }
  fWorkspace = nullptr;
  fState     = nullptr;
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

json NGnWorkspace::GetInspectorSchema() const
{
  // Build an object that contains an OpenAPI-style `properties` section
  // plus a simplified `history` (list of keys) and `group` for UI ordering.
  json out = json::object();

  // inspector wrapper
  json inspector = json::object();

  // Determine group prefix from first entry (if any)
  std::string group;
  if (!fEntries.empty()) {
    std::string nameStr = fEntries[0]->GetName();
    auto pos = nameStr.find('/');
    if (pos != std::string::npos) group = nameStr.substr(0, pos);
    else group = nameStr;
  }
  inspector["group"] = group.empty() ? "": group;

  // Properties: build OpenAPI/JSON-Schema style properties from fWorkspace
  inspector["properties"] = json::object();
  for (auto it = fWorkspace.begin(); it != fWorkspace.end(); ++it) {
    NLogDebug("Adding workspace key to inspector properties: %s", it.key().c_str());
    inspector["properties"][it.key()] = it.value();
    if (!inspector["properties"][it.key()].is_null() && !inspector["properties"][it.key()].contains("type")) {
      inspector["properties"][it.key()]["type"] = "object";
    }
  }

  // History: simplify to list of workspace keys (short names) preserving order
  json history = json::array();
  for (const auto & entry : fEntries) {
    std::string name = entry->GetName();
    std::string wsKey = name;
    if (!group.empty()) {
      std::string prefix = group + "/";
      if (wsKey.rfind(prefix, 0) == 0) wsKey = wsKey.substr(prefix.size());
    }
    history.push_back(wsKey);
  }
  inspector["history"] = history;

  out["inspector"] = inspector;

  // Attach metadata from state if present, otherwise empty object
  if (fState.is_object() && !fState.empty()) out["metadata"] = fState;
  else out["metadata"] = json::object();

  return out;
}

} // namespace Ndmspc

ClassImp(Ndmspc::NGnWorkspace);