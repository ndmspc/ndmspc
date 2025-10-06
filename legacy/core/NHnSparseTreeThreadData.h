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

class NHnSparseTreeThreadData : public NThreadData {
  public:
  NHnSparseTreeThreadData();
  virtual ~NHnSparseTreeThreadData();

  std::string GetFileName() const { return fFileName; }
  void        SetFileName(const std::string & fileName) { fFileName = fileName; }

  std::string GetEnabledBranches() const { return fEnabledBranches; }
  void        SetEnabledBranches(const std::string & enabledBranches) { fEnabledBranches = enabledBranches; }

  virtual void    Process(const std::vector<int> & coords);
  NHnSparseTree * GetHnstInput();
  NHnSparseTree * GetHnstOutput(NHnSparseTree * hnstOut = nullptr);
  void            SetHnstInput(NHnSparseTree * hnstIn) { fHnstIn = hnstIn; }
  void            SetHnstOutput(NHnSparseTree * hnstOut) { fHnstOut = hnstOut; }

  void    SetOutputGlobal(TList * outputGlobal) { fOutputGlobal = outputGlobal; }
  TList * GetOutputGlobal() const { return fOutputGlobal; }

  void     SetProcessFunc(ProcessFuncPtr func) { fProcessFunc = func; }
  Long64_t Merge(TCollection * list);

  private:
  NHnSparseTree * fHnstIn{nullptr};       ///< Pointer to the input NHnSparseTree object
  std::string     fFileName;              ///< HnSparseTree inpuy file name
  std::string     fEnabledBranches;       ///< Enabled branches in the HnSparseTree
  NHnSparseTree * fHnstOut{nullptr};      ///< Pointer to the output NHnSparseTree object
  ProcessFuncPtr  fProcessFunc{nullptr};  ///< Function pointer to the processing function
  TList *         fOutputGlobal{nullptr}; ///< Global output list for the thread

  /// \cond CLASSIMP
  ClassDef(NHnSparseTreeThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
