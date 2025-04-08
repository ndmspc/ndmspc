#ifndef Ndmspc_HnSparseTreeUtils_H
#define Ndmspc_HnSparseTreeUtils_H
#include <TObject.h>
#include <string>
#include "Config.h"
#include "HnSparseTree.h"

namespace Ndmspc {

///
/// \class HnSparseTreeUtils
///
/// \brief HnSparseTreeUtils object
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparseTreeUtils : public TObject {
  public:
  // HnSparseTreeUtils();
  // virtual ~HnSparseTreeUtils();

  static bool Read(int limit = 1000, std::string file = "/tmp/hnst.root");
  static bool ReadNew(int limit = 1000, std::string file = "/tmp/hnst.root");
  static bool Import(const Ndmspc::Config & c, std::string filename = "/tmp/hnst.root", int limit = kMaxInt,
                     std::string className = "THnSparseF");
  static bool ImportSingle(HnSparseTree * hnst, std::string filename, std::vector<std::string> objname);
  static bool Distribute(const std::string & in, const std::string & out, const std::string & filename = "hnst.root",
                         int level = 0);
  static THnSparse * ReshapeSparseAxes(THnSparse * hns, std::vector<int> order, std::vector<TAxis *> newAxes = {},
                                       std::vector<int> newPoint = {}, Option_t * option = "E");
  static void        IterateNDimensionalSpace(const std::vector<int> & minBounds, const std::vector<int> & maxBounds,
                                              const std::function<void(const std::vector<int> &)> & userFunction);
  static void IterateNDimensionalSpaceParallel(const std::vector<int> & minBounds, const std::vector<int> & maxBounds,
                                               const std::function<void(const std::vector<int> &)> & userFunction,
                                               int                                                   numThreads = -1);

  /// \cond CLASSIMP
  ClassDef(HnSparseTreeUtils, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
