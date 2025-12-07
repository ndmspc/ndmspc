#ifndef Ndmspc_NHnSparseTreeUtils_H
#define Ndmspc_NHnSparseTreeUtils_H
#include <sstream>
#include <iomanip>
#include <TObject.h>
#include <string>
#include "NLogger.h"
#include "NHnSparseTree.h"

namespace Ndmspc {

///
/// \class NHnSparseTreeUtils
///
/// \brief NHnSparseTreeUtils object
///	\author Martin Vala <mvala@cern.ch>
///

class NHnSparseTreeUtils : public TObject {
  public:
  // Stable

  /// Create ngnt.root file from points
  static NHnSparseTree * Create(std::vector<std::vector<std::string>> points, std::vector<std::string> axisNames,
                                std::vector<std::string> axisTitles = {});
  /// Create ngnt.root file from directory (just mapping)
  static bool CreateFromDir(std::string dir, std::vector<std::string> axisNames, std::string filter = "");
  static bool Reshape(std::string hnstFileNameIn, std::vector<std::string> objNames = {}, std::string directory = "",
                      std::string hnstFileNameOut = "$HOME/.ndmspc/analyses/dev/myAna/ngnt.root");

  private:
  NHnSparseTreeUtils();
  virtual ~NHnSparseTreeUtils();

  /// \cond CLASSIMP
  ClassDef(NHnSparseTreeUtils, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
