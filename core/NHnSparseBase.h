#ifndef Ndmspc_NHnSparseBase_H
#define Ndmspc_NHnSparseBase_H
#include <TObject.h>
#include "NBinning.h"
#include "NBinningPoint.h"
#include "NStorageTree.h"

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
  NHnSparseBase(NBinning * binning, NStorageTree * treeStorage);
  virtual ~NHnSparseBase();

  virtual void Print(Option_t * option = "") const override;

  NBinning *     GetBinning() const { return fBinning; }
  NStorageTree * GetStorageTree() const { return fTreeStorage; }
  TList *        GetOutput(std::string name = "");
  bool           Close(bool write = false);

  Long64_t GetEntries() const { return fTreeStorage ? fTreeStorage->GetEntries() : 0; }
  Int_t    GetEntry(Long64_t entry);

  bool Process(NHnSparseProcessFuncPtr func, std::string binningName = "", const json & cfg = json::object());
  bool Process(NHnSparseProcessFuncPtr func, std::vector<int> mins, std::vector<int> maxs,
               NBinningDef * binningDef = nullptr, const json & cfg = json::object());

  static NHnSparseBase * Open(const std::string & filename, const std::string & branches = "",
                              const std::string & treename = "hnst");

  protected:
  NBinning *                     fBinning{nullptr};     ///< Binning object
  NStorageTree *                 fTreeStorage{nullptr}; ///< Tree storage
  std::map<std::string, TList *> fOutputs;              ///< Binning definitions

  /// \cond CLASSIMP
  ClassDefOverride(NHnSparseBase, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
