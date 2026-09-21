#include "NHistoryEntry.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NHistoryEntry);
/// \endcond

namespace Ndmspc {
NHistoryEntry::NHistoryEntry(const char * name, const char * title) : TNamed(name, title) {}
NHistoryEntry::~NHistoryEntry() {}
} // namespace Ndmspc
