#include <string>
#include <THnSparse.h>
#include <TAxis.h>
#include "NDimensionalExecutor.h"

namespace Ndmspc {

// --- Private Increment Logic ---
bool NDimensionalExecutor::Increment()
{
  for (int i = fNumDimensions - 1; i >= 0; --i) {
    fCurrentCoords[i]++;
    if (fCurrentCoords[i] <= fMaxBounds[i]) {
      return true;
    }
    fCurrentCoords[i] = fMinBounds[i];
  }
  return false;
}

NDimensionalExecutor::NDimensionalExecutor(const std::vector<int> & minBounds, const std::vector<int> & maxBounds)
    : fMinBounds(minBounds), fMaxBounds(maxBounds)
{
  ///
  /// Constructor
  ///

  if (fMinBounds.size() != fMaxBounds.size()) {
    throw std::invalid_argument("Min and max bounds vectors must have the same size.");
  }
  if (fMinBounds.empty()) {
    throw std::invalid_argument("Bounds vectors cannot be empty.");
  }

  fNumDimensions = fMinBounds.size();

  for (size_t i = 0; i < fNumDimensions; ++i) {
    if (fMinBounds[i] > fMaxBounds[i]) {
      throw std::invalid_argument("Min bound (" + std::to_string(fMinBounds[i]) +
                                  ") cannot be greater than max bound (" + std::to_string(fMaxBounds[i]) +
                                  ") for dimension " + std::to_string(i));
    }
  }

  fCurrentCoords.resize(fNumDimensions);
  fCurrentCoords = fMinBounds;
}

NDimensionalExecutor::NDimensionalExecutor(THnSparse * hist, bool onlyfilled)
{
  ///
  /// Constructor with THnSparse input
  ///
  if (hist == nullptr) {
    throw std::invalid_argument("THnSparse pointer cannot be null.");
  }

  if (onlyfilled) {
    // Check if the histogram is filled
    if (hist->GetNbins() <= 0) {
      throw std::invalid_argument("THnSparse histogram is empty.");
    }

    fMinBounds.push_back(0);
    fMaxBounds.push_back(hist->GetNbins());
  }
  else {
    // loop over all dimensions
    for (int i = 0; i < hist->GetNdimensions(); ++i) {
      fMinBounds.push_back(0);
      fMaxBounds.push_back(hist->GetAxis(i)->GetNbins());
    }
  }
  fNumDimensions = fMinBounds.size();
  fCurrentCoords.resize(fNumDimensions);
  fCurrentCoords = fMinBounds;
}

void NDimensionalExecutor::Execute(const std::function<void(const std::vector<int> & coords)> & func)
{
  ///
  /// Sequential Execution
  ///

  if (fNumDimensions == 0) {
    return;
  }
  fCurrentCoords = fMinBounds; // Reset state
  do {
    func(fCurrentCoords);
  } while (Increment());
}

} // namespace Ndmspc
