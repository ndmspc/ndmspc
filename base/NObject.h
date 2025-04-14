#ifndef Ndmspc_NObject_H
#define Ndmspc_NObject_H
#include <TObject.h>
#include "NLogger.h"

namespace Ndmspc {

///
/// \class NObject
///
/// \brief NObject object
///	\author Martin Vala <mvala@cern.ch>
///

class NObject : public TObject {
  public:
  NObject();
  virtual ~NObject();

  private:
  NLogger *fLogger; ///< Logger object

  /// \cond CLASSIMP
  ClassDef(NObject, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
