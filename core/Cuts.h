#ifndef NdmspcCuts_H
#define NdmspcCuts_H
#include <TObject.h>

#include "Axis.h"

namespace Ndmspc {

///
/// \class Cuts
///
/// \brief Cuts object
///	\author Martin Vala <mvala@cern.ch>
///

class Cuts : public TObject {
  public:
  Cuts();
  virtual ~Cuts();

  virtual void Print(Option_t * option = "") const;
  /// Add axis
  void AddAxis(Ndmspc::Axis * axis) { fAxes.push_back(axis); }
  /// Return axis
  Axis * GetAxis(int i) const { return fAxes[i]; }

  private:
  /// List of axes
  std::vector<Ndmspc::Axis *> fAxes{};

  /// \cond CLASSIMP
  ClassDef(Cuts, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
