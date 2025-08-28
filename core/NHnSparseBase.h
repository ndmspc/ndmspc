#ifndef Ndmspc_NHnSparseBase_H
#define Ndmspc_NHnSparseBase_H
#include <TObject.h>
#include "NBinning.h"
#include "NBinningPoint.h"

namespace Ndmspc {

///
/// \class NHnSparseBase
///
/// \brief NHnSparseBase object
///	\author Martin Vala <mvala@cern.ch>
///

using NHnSparseProcessFuncPtr = void (*)(Ndmspc::NBinningPoint *, TList *, TList *, int);
class NHnSparseBase : public TObject {
  public:
  NHnSparseBase();
  NHnSparseBase(std::vector<TAxis *> axes);
  NHnSparseBase(TObjArray * axes);
  virtual ~NHnSparseBase();

  virtual void Print(Option_t * option = "") const override;

  NBinning * GetBinning() const { return fBinning; }

  bool Process(NHnSparseProcessFuncPtr func, std::string binningName = "");
  bool Process(NHnSparseProcessFuncPtr func, std::vector<int> mins, std::vector<int> maxs,
               NBinningDef * binningDef = nullptr);

  protected:
  NBinning * fBinning{nullptr}; ///< Binning object

  /// \cond CLASSIMP
  ClassDefOverride(NHnSparseBase, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
