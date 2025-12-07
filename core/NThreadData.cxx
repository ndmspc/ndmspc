#include <iostream>
#include <vector>
#include <thread>
#include <TString.h>
#include "NLogger.h"
#include "NThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NThreadData);
/// \endcond
namespace Ndmspc {
std::mutex NThreadData::fSharedMutex;
NThreadData::NThreadData() : TObject(), fItemCount(0), fCoordSum(0), fThreadId(), fIdSet(false), fAssignedIndex(0)
{
  ///
  /// Constructor
  ///
}

NThreadData::~NThreadData()
{
  ///
  /// Destructor
  ///
}
void NThreadData::Process(const std::vector<int> & coords)
{

  NLogTrace("Processing coordinates in thread %llu",
                 (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()));
  // Use renamed members with lowercase 'f'
  if (!fIdSet) {
    fThreadId = std::this_thread::get_id();
    fIdSet    = true;
  }
  fItemCount++;
  for (int val : coords) {
    fCoordSum += val;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void NThreadData::Print(Option_t * /*option*/) const
{
  NLogTrace("NThreadData [Index: %zu, ThreadId: %llu, Items: %lld, Sum: %lld]", fAssignedIndex,
                 (unsigned long long)std::hash<std::thread::id>{}(fThreadId), fItemCount, fCoordSum);
}

} // namespace Ndmspc
