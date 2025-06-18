#ifndef NHnSparseTree_H
#define NHnSparseTree_H
#include <map>
#include <vector>
#include <TSystem.h>
#include <THnSparse.h>
#include <TTree.h>
#include <TBranch.h>
#include "NBinning.h"
#include "NHnSparseTreePoint.h"
#include "NUtils.h"
#include "NTreeBranch.h"

namespace Ndmspc {

///
/// \class HnSparse
///
/// \brief HnSparse object
///	\author Martin Vala <mvala@cern.ch>
///

class NHnSparseTree : public THnSparse {

  public:
  NHnSparseTree();
  virtual ~NHnSparseTree();

  NHnSparseTree(const std::string & filename, const std::string & treename = "hnst");
  NHnSparseTree(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin = 0,
                const Double_t * xmax = 0, Int_t chunksize = 1024 * 16);

  static NHnSparseTree * Open(const std::string & filename, const std::string & branches = "",
                              const std::string & treename = "hnst");
  virtual void           Print(Option_t * option = "") const;

  bool     Import(std::string filename, std::string directory, std::vector<std::string> objNames,
                  std::map<std::string, std::vector<std::vector<int>>> binning = {{}});
  bool     InitTree(const std::string & filename = "", const std::string & treename = "hnst");
  bool     InitAxes(TObjArray * newAxes, int n = 0);
  bool     InitBinnings(std::vector<TAxis *> axes);
  Int_t    FillTree();
  Long64_t GetEntry(Long64_t entry);
  Long64_t GetEntries() const { return fTree ? fTree->GetEntries() : 0; }
  bool     Close(bool write = false);

  /// Setting output file name
  void SetFileName(const std::string & fn) { fFileName = fn; }
  /// Returns output filename
  std::string GetFileName() const { return fFileName; }

  /// Setting File and tree
  bool SetFileTree(TFile * file, TTree * tree, bool force = false);
  /// Returns file
  TFile * GetFile() const { return fFile; }
  /// Returns tree
  TTree * GetTree() const { return fTree; }
  /// Returns binning
  NBinning * GetBinning() const { return fBinning; }

  /// Setting prefix path
  void SetPrefix(const std::string & prefix) { fPrefix = prefix; }
  /// Gettting prefix path
  std::string GetPrefix() const { return fPrefix; }
  /// Setting postfix path
  void SetPostfix(const std::string & postfix) { fPostfix = postfix; }
  /// Getting postfix path
  std::string GetPostfix() const { return fPostfix; }
  std::string GetFullPath(std::vector<int> coords) const;
  std::string GetPointStr(const std::vector<int> & coords) const;
  void GetPointMinMax(const std::vector<int> & coords, std::vector<double> & min, std::vector<double> & max) const;

  // void                 SetPointAt(int i, int val) { fPoint[i] = val; }
  // void                 SetPoint(std::vector<int> coords) { NUtils::VectorToArray(coords, fPoint); }
  // Int_t *              GetPoint() { return fPoint; }
  // std::vector<int>     GetPointVector() const { return NUtils::ArrayToVector(fPoint, GetNdimensions()); }
  NHnSparseTreePoint * GetPoint() const { return fPointData; }

  /// Get list of branch names
  std::vector<std::string> GetBrancheNames();
  bool                     AddBranch(const std::string & name, void * address, const std::string & className);
  NTreeBranch *            GetBranch(const std::string & name) { return &fBranchesMap[name]; }
  TObject *                GetBranchObject(const std::string & name);
  void                     SetEnabledBranches(std::vector<std::string> branches);
  void                     SetBranchAddresses();
  void SaveEntry(NHnSparseTree * hnstIn, std::vector<std::vector<int>> ranges, bool useProjection = false);

  private:
  std::string fFileName{"hnst.root"}; ///< Current filename
  TFile *     fFile{nullptr};         ///<! Current file
  std::string fPrefix{""};            ///< Prefix path
  std::string fPostfix{""};           ///< Postfix path
  TTree *     fTree{nullptr};         ///<! Content container
  // Int_t *              fPoint{nullptr};        ///<! Point
  NBinning *           fBinning{nullptr};   ///< Binning
  NHnSparseTreePoint * fPointData{nullptr}; ///< Point data
  ///
  std::map<std::string, NTreeBranch> fBranchesMap; ///< Branches map

  /// \cond CLASSIMP
  ClassDef(NHnSparseTree, 1);
  /// \endcond
};

//______________________________________________________________________________
/** \class HnSparseT
 Templated implementation of the abstract base THnSparse.
 All functionality and the interfaces to be used are in THnSparse!

 THnSparse does not know how to store any bin content itself. Instead, this
 is delegated to the derived, templated class: the template parameter decides
 what the format for the bin content is. In fact it even defines the array
 itself; possible implementations probably derive from TArray.

 Typedefs exist for template parameters with ROOT's generic types:

 Templated name      |    Typedef   |    Bin content type
 --------------------|--------------|--------------------
 HnSparseT<TArrayC>   |  HnSparseC   |  Char_t
 HnSparseT<TArrayS>   |  HnSparseS   |  Short_t
 HnSparseT<TArrayI>   |  HnSparseI   |  Int_t
 HnSparseT<TArrayL64> |  HnSparseL   |  Long64_t
 HnSparseT<TArrayF>   |  HnSparseF   |  Float_t
 HnSparseT<TArrayD>   |  HnSparseD   |  Double_t

 We recommend to use THnSparseC wherever possible, and to map its value space
 of 256 possible values to e.g. float values outside the class. This saves an
 enormous amount of memory. Only if more than 256 values need to be
 distinguished should e.g. THnSparseS or even THnSparseF be chosen.

 Implementation detail: the derived, templated class is kept extremely small
 on purpose. That way the (templated thus inlined) uses of this class will
 only create a small amount of machine code, in contrast to e.g. STL.
*/

template <class CONT>
class NHnSparseTreeT : public NHnSparseTree {
  public:
  using NHnSparseTree::NHnSparseTree;
  /// /// Default constructor
  /// NHnSparseTreeT() {}
  /// /// Constructor
  /// NHnSparseTreeT(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin =
  /// nullptr,
  ///               const Double_t * xmax = nullptr, Int_t chunksize = 1024 * 16)
  ///     : NHnSparseTree(name, title, dim, nbins, xmin, xmax, chunksize)
  /// {
  /// }
  ///
  /// NHnSparseTreeT(const std::string & filename, const std::string & treename = "ndh") : NHnSparseTree(filename,
  /// treename)
  /// {
  /// }

  /// Returns content array
  TArray * GenerateArray() const override { return new CONT(GetChunkSize()); }

  private:
  /// \cond CLASSIMP
  ClassDefOverride(NHnSparseTreeT, 1); // Sparse n-dimensional histogram with templated content
  /// \endcond;
};

typedef NHnSparseTreeT<TArrayD> NHnSparseTreeD;
typedef NHnSparseTreeT<TArrayF> NHnSparseTreeF;
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 32, 0)
typedef NHnSparseTreeT<TArrayL64> NHnSparseTreeL;
#else
typedef HnSparseT<TArrayL> NHnSparseTreeL;
#endif
typedef NHnSparseTreeT<TArrayI> NHnSparseTreeI;
typedef NHnSparseTreeT<TArrayS> NHnSparseTreeS;
typedef NHnSparseTreeT<TArrayC> NHnSparseTreeC;
} // namespace Ndmspc

#endif /* HNSPARSE_H */
