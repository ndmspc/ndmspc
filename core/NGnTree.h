#ifndef Ndmspc_NGnTree_H
#define Ndmspc_NGnTree_H
#include <TObject.h>
#include "NBinning.h"
#include "NBinningPoint.h"
#include "NStorageTree.h"

namespace Ndmspc {

///
/// \class NGnTree
///
/// \brief NGnTree object
///	\author Martin Vala <mvala@cern.ch>
///

using NHnSparseProcessFuncPtr = void (*)(Ndmspc::NBinningPoint *, TList *, TList *, int);
class NGnTree : public TObject {
  public:
  NGnTree();
  NGnTree(std::vector<TAxis *> axes, std::string filename = "", std::string treename = "hnst");
  NGnTree(TObjArray * axes, std::string filename = "", std::string treename = "hnst");
  NGnTree(NBinning * binning, NStorageTree * treeStorage);
  virtual ~NGnTree();

  virtual void Print(Option_t * option = "") const override;
  void         Play(int timeout = 0, std::string binning = "", Option_t * option = "", std::string ws = "");

  NBinning *                     GetBinning() const { return fBinning; }
  NStorageTree *                 GetStorageTree() const { return fTreeStorage; }
  std::map<std::string, TList *> GetOutputs() const { return fOutputs; }
  TList *                        GetOutput(std::string name = "");
  void                           SetOutputs(std::map<std::string, TList *> outputs) { fOutputs = outputs; }

  Long64_t GetEntries() const { return fTreeStorage ? fTreeStorage->GetEntries() : 0; }
  Int_t    GetEntry(Long64_t entry, bool checkBinningDef = true);
  bool     Close(bool write = false);

  bool Process(NHnSparseProcessFuncPtr func, const json & cfg = json::object(), std::string binningName = "");
  bool Process(NHnSparseProcessFuncPtr func, const std::vector<std::string> & defNames,
               const json & cfg = json::object(), NBinning * hnsbBinningIn = nullptr);

  static NGnTree * Open(const std::string & filename, const std::string & branches = "",
                              const std::string & treename = "hnst");
  static NGnTree * Open(TTree * tree, const std::string & branches = "", TFile * file = nullptr);

  protected:
  NBinning *                     fBinning{nullptr};     ///< Binning object
  NStorageTree *                 fTreeStorage{nullptr}; ///< Tree storage
  std::map<std::string, TList *> fOutputs;              ///< Outputs

  /// \cond CLASSIMP
  ClassDefOverride(NGnTree, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
