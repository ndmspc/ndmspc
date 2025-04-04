#ifndef NdmspcAxes_H
#define NdmspcAxes_H
#include <TObject.h>

#include "Axis.h"

namespace Ndmspc {

///
/// \class Axes
///
/// \brief Axes object
///	\author Martin Vala <mvala@cern.ch>
///

class Axes : public TObject {
  public:
  Axes();
  virtual ~Axes();

  virtual void Print(Option_t * option = "") const;
  /// Add axis
  void AddAxis(Ndmspc::Axis axis) { fAxes.push_back(axis); }
  /// Return axis
  Axis *      GetAxis(int i) { return &fAxes[i]; }
  std::string GetPath(int * point);

  std::vector<Ndmspc::Axis> GetAxes() const { return fAxes; }

  private:
  /// List of axes
  std::vector<Ndmspc::Axis> fAxes{};

  /// \cond CLASSIMP
  ClassDef(Axes, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
