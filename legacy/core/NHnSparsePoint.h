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
class NGnTree;
class NHnSparsePoint : public TObject {
  public:
  NHnSparsePoint(NGnTree * in = nullptr, NGnTree * out = nullptr);
  virtual ~NHnSparsePoint();

  virtual void Print(Option_t * option = "") const;

  NGnTree * GetInput() const { return fIn; }
  void            SetInput(NGnTree * in) { fIn = in; }

  NGnTree * GetOutput() const { return fOut; }
  void            SetOutput(NGnTree * out) { fOut = out; }

  private:
  NGnTree * fIn{nullptr};  ///< Input object
  NGnTree * fOut{nullptr}; ///< Output object

  /// \cond CLASSIMP
  ClassDef(NHnSparsePoint, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
