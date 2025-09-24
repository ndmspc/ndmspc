#ifndef Ndmspc_NHnSparseThreadData_H
#define Ndmspc_NHnSparseThreadData_H
// #include "NBinning.h"
// #include "NStorageTree.h"
#include "NHnSparseBase.h"
#include "NThreadData.h"

namespace Ndmspc {

///
/// \class NHnSparseThreadData
///
/// \brief NHnSparseThreadData object
///	\author Martin Vala <mvala@cern.ch>
///

class NHnSparseThreadData : public NThreadData {
  public:
  NHnSparseThreadData();
  virtual ~NHnSparseThreadData();
  bool Init(size_t id, NHnSparseProcessFuncPtr func, NHnSparseBase * hnsb, const std::string & filename = "",
            const std::string & treename = "hnst");

  void SetProcessFunc(NHnSparseProcessFuncPtr func) { fProcessFunc = func; }
  // void SetHnSparseBase(NHnSparseBase * hnsb) { fHnSparseBase = hnsb; }

  // void SetOutput(TList * output) { fOutput = output; }
  // void SetBinning(NBinning * binning) { fBinning = binning; }
  // void SetBinningDef(NBinningDef * def) { fBinningDef = def; }

  bool InitStorage(NStorageTree * ts, const std::string & filename = "", const std::string & treename = "hnst");

  // TList *        GetOutput() const { return fOutput; }
  // NStorageTree * GetTreeStorage() const { return fTreeStorage; }
  // NBinning *     GetBinning() const { return fBinning; }
  NHnSparseBase * GetHnSparseBase() const { return fHnSparseBase; }
  Long64_t        GetNProcessed() const { return fNProcessed; }

  virtual void     Process(const std::vector<int> & coords);
  virtual Long64_t Merge(TCollection * list);

  private:
  NHnSparseProcessFuncPtr fProcessFunc{nullptr};  ///< Function pointer to the processing function
  NHnSparseBase *         fHnSparseBase{nullptr}; ///< Pointer to the base class
  Long64_t                fNProcessed{0};         ///< Number of processed entries
  NBinning *              fBiningSource{nullptr}; ///< Pointer to the source binning (from the original NHnSparseBase)

  // TList *        fOutput{nullptr};      ///< Global output list for the thread
  // NBinning *     fBinning{nullptr};     ///< Binning object for the thread
  // NStorageTree * fTreeStorage{nullptr}; ///< Tree storage for the thread
  // NBinningDef *  fBinningDef{nullptr};  ///< Binning definition for the thread

  /// \cond CLASSIMP
  ClassDef(NHnSparseThreadData, 1);
  /// \endcond;
};
} // namespace Ndmspc
#endif
