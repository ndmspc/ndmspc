#ifndef Ndmspc_NBinning_H
#define Ndmspc_NBinning_H
#include <TObject.h>
#include <THnSparse.h>
#include <TAxis.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <map>
#include "TObjArray.h"

namespace Ndmspc {

///
/// \class NBinning
///
/// \brief NBinning object
///	\author Martin Vala <mvala@cern.ch>
///

enum class Binning {
  kSingle   = 0, ///< No rebin possible
  kMultiple = 1, ///< Rebin is possible
};

class NBinning : public TObject {
  public:
  NBinning();
  NBinning(std::vector<TAxis *> axes);
  virtual ~NBinning();

  virtual void                  Print(Option_t * option = "") const;
  void                          PrintContent(Option_t * option = "") const;
  int                           FillAll();
  std::vector<std::vector<int>> GetCoordsRange(std::vector<int> c) const;
  std::vector<std::vector<int>> GetAxisRanges(std::vector<int> c) const;
  TObjArray *                   GetListOfAxes() const;
  std::vector<int>              GetAxisBinning(int axisId, const std::vector<int> & c) const;

  bool AddBinning(int id, std::vector<int> binning, int n = 1);
  bool AddBinningVariable(int id, std::vector<int> mins);
  bool AddBinningViaBinWidths(int id, std::vector<std::vector<int>> widths);

  /// Returns the mapping histogram
  THnSparse *                                            GetMap() const { return fMap; }
  THnSparse *                                            GetContent() const { return fContent; }
  std::vector<TAxis *>                                   GetAxes() const { return fAxes; }
  Binning                                                GetBinningType(int i) const;
  std::map<std::string, std::vector<std::vector<int>>> & GetDefinition() { return fDefinition; }

  private:
  THnSparse *                                          fMap{nullptr};     ///< Mapping histogram
  THnSparse *                                          fContent{nullptr}; ///< Content histogram
  std::vector<TAxis *>                                 fAxes;             ///< List of axes
  std::vector<Binning>                                 fBinningTypes;     ///< Binning types
  std::map<std::string, std::vector<std::vector<int>>> fDefinition;       ///< Binning mapping definition

  /// \cond CLASSIMP
  ClassDef(NBinning, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
