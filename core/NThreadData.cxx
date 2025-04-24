#include <iostream>
#include <vector>
#include <thread>
#include <TString.h>
#include "NThreadData.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NThreadData);
/// \endcond

namespace Ndmspc {
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
  TString info;
  // Use renamed members with lowercase 'f'
  info.Form("NThreadData [Index: %zu, ThreadId: %llu, Items: %lld, Sum: %lld]", fAssignedIndex,
            (unsigned long long)std::hash<std::thread::id>{}(fThreadId), fItemCount, fCoordSum);
  std::cout << info.Data() << std::endl;
}

} // namespace Ndmspc
