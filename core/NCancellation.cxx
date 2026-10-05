#include "NCancellation.h"

namespace Ndmspc {

std::atomic<const std::atomic<bool> *> NCancellation::fCurrent{nullptr};

void NCancellation::SetCurrent(const std::atomic<bool> * token)
{
  fCurrent.store(token, std::memory_order_release);
}

void NCancellation::ClearCurrent()
{
  fCurrent.store(nullptr, std::memory_order_release);
}

bool NCancellation::IsCancelled()
{
  const std::atomic<bool> * token = fCurrent.load(std::memory_order_acquire);
  return token != nullptr && token->load(std::memory_order_relaxed);
}

} // namespace Ndmspc
