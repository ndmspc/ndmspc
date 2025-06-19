#ifndef Ndmspc_NHnSparseTreeThreadData_H
#define Ndmspc_NHnSparseTreeThreadData_H
#include <NThreadData.h>
#include <mutex>
#include <vector>
#include "NHnSparseTree.h"

namespace Ndmspc {

///
/// \class NHnSparseTreeThreadData
///
/// \brief NHnSparseTreeThreadData object
///	\author Martin Vala <mvala@cern.ch>
///

using ProcessFuncPtr = void (*)(Ndmspc::NHnSparseTreePoint *, int);
class NHnSparseTreeThreadData : public NThreadData {
  public:
  NHnSparseTreeThreadData();
  virtual ~NHnSparseTreeThreadData();

  std::string GetFileName() const { return fFileName; }
  void        SetFileName(const std::string & fileName) { fFileName = fileName; }

  std::string GetEnabledBranches() const { return fEnabledBranches; }
  void        SetEnabledBranches(const std::string & enabledBranches) { fEnabledBranches = enabledBranches; }

  virtual void    Process(const std::vector<int> & coords);
  NHnSparseTree * GetHnSparseTree();
  void            SetMacro(TMacro * macro) { fMacro = macro; }

  void SetProcessFunc(ProcessFuncPtr func) { fProcessFunc = func; }

  private:
  NHnSparseTree * fHnSparseTree{nullptr}; ///< Pointer to the NHnSparseTree object
  TMacro *        fMacro{nullptr};        ///< Pointer to the macro to be executed
  std::string     fFileName;              ///< HnSparseTree file name
  std::string     fEnabledBranches;       ///< Enabled branches in the HnSparseTree
  ProcessFuncPtr  fProcessFunc{nullptr};  ///< Function pointer to the processing function

  /// \cond CLASSIMP
  ClassDef(NHnSparseTreeThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
