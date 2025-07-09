#ifndef Ndmspc_NHnSparseObject_H
#define Ndmspc_NHnSparseObject_H
#include <TObject.h>
#include <vector>
#include "NBinning.h"

namespace Ndmspc {

///
/// \class NHnSparseObject
///
/// \brief NHnSparseObject object
///	\author Martin Vala <mvala@cern.ch>
///
class NHnSparseTreePoint;
class NHnSparseObject : public TObject {
  public:
  NHnSparseObject(std::vector<TAxis *> axes = {});
  NHnSparseObject(TObjArray * axes);
  virtual ~NHnSparseObject();

  virtual void Print(Option_t * option = "") const;

  int                    Fill(TH1 * h, NHnSparseTreePoint * point = nullptr);
  std::vector<TObject *> GetObjects(const std::string & name) const;
  TObject *              GetObject(const std::string & name, int index = 0) const;
  NBinning *             GetBinning() const { return fBinning; }

  private:
  NBinning *                                    fBinning{nullptr}; ///< Binning object
  std::map<std::string, std::vector<TObject *>> fObjectContentMap; ///< Object content map

  /// \cond CLASSIMP
  ClassDef(NHnSparseObject, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
