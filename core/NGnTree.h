#ifndef Ndmspc_NGnTree_H
#define Ndmspc_NGnTree_H
#include <TObject.h>
#include <THnSparse.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
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
  NGnTree(NGnTree * ngnt, std::string filename = "", std::string treename = "hnst");
  NGnTree(NBinning * binning, NStorageTree * treeStorage);
  virtual ~NGnTree();

  virtual void Print(Option_t * option = "") const override;
  void         Play(int timeout = 0, std::string binning = "", std::vector<int> outputPointIds = {0},
                    std::vector<std::vector<int>> ranges = {}, Option_t * option = "", std::string ws = "");

  NBinning *                     GetBinning() const { return fBinning; }
  void                           SetBinning(NBinning * binning) { fBinning = binning; }
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

  static NGnTree *         Open(const std::string & filename, const std::string & branches = "",
                                const std::string & treename = "hnst");
  static NGnTree *         Open(TTree * tree, const std::string & branches = "", TFile * file = nullptr);
  std::vector<THnSparse *> GetTHnSparseFromObjects(const std::vector<std::string> & names,
                                                   std::map<int, std::vector<int>> ranges = {}, bool rangeReset = true,
                                                   bool modifyTitle = false);
  std::vector<TH1 *> ProjectionFromObjects(const std::vector<std::string> & names, int xaxis, Option_t * option = "O",
                                           std::map<int, std::vector<int>> ranges = {}, bool rangeReset = true,
                                           bool modifyTitle = false);
  std::vector<TH2 *> ProjectionFromObjects(const std::vector<std::string> & names, int yaxis, int xaxis,
                                           Option_t * option = "O", std::map<int, std::vector<int>> ranges = {},
                                           bool rangeReset = true, bool modifyTitle = false);
  std::vector<TH3 *> ProjectionFromObjects(const std::vector<std::string> & names, int xaxis, int yaxis, int zaxis,
                                           Option_t * option = "O", std::map<int, std::vector<int>> ranges = {},
                                           bool rangeReset = true, bool modifyTitle = false);

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
