#ifndef Ndmspc_NBinning_H
#define Ndmspc_NBinning_H
#include <TObject.h>
#include <THnSparse.h>
#include <TAxis.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace Ndmspc {

///
/// \class NBinning
///
/// \brief NBinning object
///	\author Martin Vala <mvala@cern.ch>
///
class NBinning : public TObject {
  public:
  NBinning(std::vector<TAxis *> axes = {});
  virtual ~NBinning();

  virtual void                  Print(Option_t * option = "") const;
  void                          PrintContent(Option_t * option = "") const;
  std::vector<std::vector<int>> GetAll();
  int                           FillAll();

  bool AddBinning(std::vector<int> binning, int n = 1);

  /// Returns the mapping histogram
  THnSparse *          GetMap() const { return fMap; }
  THnSparse *          GetContent() const { return fContent; }
  std::vector<TAxis *> GetAxes() const { return fAxes; }

  private:
  THnSparse *          fMap{nullptr};     ///< Mapping histogram
  THnSparse *          fContent{nullptr}; ///< Content histogram
  std::vector<TAxis *> fAxes;             ///< List of axes

  /// \cond CLASSIMP
  ClassDef(NBinning, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
