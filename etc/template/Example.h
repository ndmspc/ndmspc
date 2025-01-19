#ifndef NdmspcExample_H
#define NdmspcExample_H
#include <TObject.h>

namespace Ndmspc {

///
/// \class Example
///
/// \brief Example object
///	\author Martin Vala <mvala@cern.ch>
///

class Example : public TObject {
  public:
  Example();
  virtual ~Example();

  /// \cond CLASSIMP
  ClassDef(Example, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
