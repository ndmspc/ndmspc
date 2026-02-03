#include "NGnHistoryEntry.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NGnHistoryEntry);
/// \endcond

namespace Ndmspc {
NGnHistoryEntry::NGnHistoryEntry(const char * name, const char * title) : TNamed(name, title) {}
NGnHistoryEntry::~NGnHistoryEntry() {}
} // namespace Ndmspc
