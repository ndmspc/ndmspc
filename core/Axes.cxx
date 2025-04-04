#include <TString.h>
#include <string>
#include "Axes.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::Axes);
/// \endcond

namespace Ndmspc {
Axes::Axes() : TObject()
{
  ///
  /// Constructor
  ///
}
Axes::~Axes()
{
  ///
  /// Destructor
  ///
}
std::string Axes::GetPath(int * point)
{
  ///
  /// Get path
  ///
  std::string path;
  path += std::to_string(fAxes.size()) + "/";

  int i = 0;
  for (auto axis : fAxes) {
    path += std::to_string(i);

    // TODO: Decide if we want to include rebin
    //       and rebin start in the path if they are '1'
    if (axis.GetRebin() > 1) {
      path += "-";
      path += std::to_string(axis.GetRebin()) + "-";
      path += std::to_string(axis.GetRebinStart());
    }
    path += "/";
    i++;
  }

  // loop over point and add to path
  for (int i = 0; i < fAxes.size(); i++) {
    path += std::to_string(point[i]) + "/";
  }

  if (path.size() > 0) {
    path.pop_back();
  }
  return path;
}
void Axes::Print(Option_t * option) const
{
  /// Print cuts info

  for (auto axis : fAxes) {
    axis.Print(option);
  }
}
} // namespace Ndmspc
