#ifndef Ndmspc_NHnSparsePoint_H
#define Ndmspc_NHnSparsePoint_H
#include <TObject.h>

namespace Ndmspc {

///
/// \class NHnSparsePoint
///
/// \brief NHnSparsePoint object
///	\author Martin Vala <mvala@cern.ch>
///
class NHnSparseBase;
class NHnSparsePoint : public TObject {
  public:
  NHnSparsePoint(NHnSparseBase * in = nullptr, NHnSparseBase * out = nullptr);
  virtual ~NHnSparsePoint();

  virtual void Print(Option_t * option = "") const;

  NHnSparseBase * GetInput() const { return fIn; }
  void            SetInput(NHnSparseBase * in) { fIn = in; }

  NHnSparseBase * GetOutput() const { return fOut; }
  void            SetOutput(NHnSparseBase * out) { fOut = out; }

  private:
  NHnSparseBase * fIn{nullptr};  ///< Input object
  NHnSparseBase * fOut{nullptr}; ///< Output object

  /// \cond CLASSIMP
  ClassDef(NHnSparsePoint, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
