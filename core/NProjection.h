#ifndef Ndmspc_NProjection_H
#define Ndmspc_NProjection_H
#include <TGClient.h>
#include "NHnSparseObject.h"

namespace Ndmspc {

///
/// \class NProjection
///
/// \brief NProjection object
///	\author Martin Vala <mvala@cern.ch>
///

class NProjection : public NHnSparseObject {
  public:
  using NHnSparseObject::NHnSparseObject;
  // NProjection();
  // virtual ~NProjection();
  //
  virtual void Draw(Option_t * option = "") const;
  virtual void Draw(std::vector<std::string> projNames, Option_t * option = "") const;
  virtual void Draw(std::vector<int> projIds, Option_t * option = "") const;

  /// \cond CLASSIMP
  ClassDef(NProjection, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
