#include "NWorkspace.h"
#include "ndmspc/http/NHttpServer.h"

#include <algorithm>
#include <fstream>

#include "ndmspc/core/NLogger.h"

namespace {

/// @brief The group a route belongs to: the part before its first '/', or the whole name.
///
/// Each tool group is its own chain of steps, so a re-run invalidates the steps that followed it
/// **in its own group** - not another group's, whose session (and open file) is its own.
std::string RouteGroup(const std::string & name)
{
  const auto slash = name.find('/');
  return slash == std::string::npos ? name : name.substr(0, slash);
}

} // namespace

/// \cond CLASSIMP
ClassImp(Ndmspc::NWorkspace);
/// \endcond

namespace Ndmspc {

NWorkspace::NWorkspace(const char * name, const char * title, NHttpServer * server)
    : TNamed(name, title), fServer(server)
{
}

NWorkspace::~NWorkspace()
{
  Clear();
}

void NWorkspace::Print(Option_t * option) const
{
  (void)option;
  NLogInfo("NWorkspace with %zu entries:", fEntries.size());
  for (size_t i = 0; i < fEntries.size(); i++) {
    NLogInfo("  [%zu]: %s payload: in=%s out=%s", i, fEntries[i]->GetName(), fEntries[i]->GetPayloadIn().dump().c_str(),
             fEntries[i]->GetPayloadOut().dump().c_str());
  }
  NLogInfo("Workspace: %s", GetWorkspace().dump().c_str());
  NLogInfo("State: %s", GetState().dump().c_str());
}

void NWorkspace::AddEntry(NHistoryEntry * entry)
{
  if (entry) {
    RemoveEntry(entry->GetName());
    NLogTrace("Adding workspace entry: %s", entry->GetName());
    NLogTrace("Config: %s", entry->GetPayloadIn().dump().c_str());
    fEntries.push_back(entry);
  }
}

bool NWorkspace::RemoveEntry(int index)
{
  if (index < 0 || index >= static_cast<int>(fEntries.size())) {
    NLogError("Invalid workspace entry index: %d", index);
    return false;
  }

  NHistoryEntry * entry = fEntries.at(index);
  json              in    = entry->GetPayloadIn();
  json              out;
  json              wsOut;
  NLogTrace("Removing workspace entry: %s", entry->GetName());
  NLogTrace("Config: %s", in.dump().c_str());
  NLogTrace("Invoking HTTP handler for DELETE on entry: %s", entry->GetName());
  const auto handlerFn = fServer->FindHttpHandler(entry->GetName());
  if (handlerFn) handlerFn("DELETE", in, out, wsOut, fServer->GetObjectsMap());

  // if (fWorkspace.contains(entry->GetName())) fWorkspace.erase(entry->GetName());
  delete entry;
  fEntries.erase(fEntries.begin() + index);

  // Remove schemas for entries that have been deleted. History entries use
  // full route names ("ngnt/open"), while workspace keys use the route name
  // relative to that group ("open").
  std::vector<std::string> orphanedKeys;
  for (auto it = fWorkspace.begin(); it != fWorkspace.end(); ++it) {
    bool hasEntry = false;
    for (const auto & e : fEntries) {
      std::string entryKey = e->GetName();
      const auto  slashPos = entryKey.find('/');
      if (slashPos != std::string::npos) entryKey = entryKey.substr(slashPos + 1);

      if (it.key() == entryKey) {
        hasEntry = true;
        break;
      }
    }
    if (!hasEntry) orphanedKeys.push_back(it.key());
  }
  for (const auto & key : orphanedKeys) {
    NLogTrace("Removing orphaned workspace key: %s", key.c_str());
    fWorkspace.erase(key);
  }

  return true;
}

bool NWorkspace::RemoveEntry(const std::string & name)
{
  // Roll this route back **in its own group**: the entry itself, and the steps that followed it there.
  // Another group's entries are left alone, because its session is its own - and tearing them down
  // would run their DELETE handlers, which close the file that group has open.
  const std::string group = RouteGroup(name);
  for (int i = static_cast<int>(fEntries.size()) - 1; i >= 0; i--) {
    if (fEntries.at(i)->GetName() != name) continue;
    NLogTrace("Found existing workspace entry with same name: %s at index %d, removing its newer steps.", name.c_str(),
              i);
    for (int j = static_cast<int>(fEntries.size()) - 1; j > i; j--) {
      if (RouteGroup(fEntries.at(j)->GetName()) != group) continue; // another group's step: not ours to undo
      NLogTrace("Removing workspace entry at index %d: %s", j, fEntries.at(j)->GetName());
      RemoveEntry(j);
    }
    RemoveEntry(i);
    return true;
  }
  return false;
}

bool NWorkspace::HasEntry(const std::string & name) const
{
  for (const auto * entry : fEntries) {
    if (entry && name == entry->GetName()) return true;
  }
  return false;
}

void NWorkspace::Clear(Option_t *)
{
  for (int i = static_cast<int>(fEntries.size()) - 1; i >= 0; i--) {
    RemoveEntry(i);
  }
  fWorkspace = nullptr;
  fState     = nullptr;
  fCombinations = nullptr;
}

bool NWorkspace::LoadFromFile(const std::string & filename)
{
  (void)filename;
  // Implement loading logic as needed
  return false;
}

bool NWorkspace::ExportToFile(const std::string & filename) const
{
  (void)filename;
  // Implement export logic as needed
  return false;
}

json NWorkspace::GetInspectorSchema() const
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
    auto        pos     = nameStr.find('/');
    if (pos != std::string::npos)
      group = nameStr.substr(0, pos);
    else
      group = nameStr;
  }
  inspector["group"] = group.empty() ? "" : group;

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
    std::string name  = entry->GetName();
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
  if (fState.is_object() && !fState.empty())
    out["metadata"] = fState;
  else
    out["metadata"] = json::object();

  return out;
}

} // namespace Ndmspc
