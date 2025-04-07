#ifndef HnSparseTree_H
#define HnSparseTree_H

#include <TObject.h>
#include <TSystem.h>
#include <TTree.h>
#include <THnSparse.h>
#include <TBranch.h>
#include <map>
#include <vector>
#include "HnSparseTreeBranch.h"
#include "Axes.h"

namespace Ndmspc {

///
/// \class HnSparse
///
/// \brief HnSparse object
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparseTree : public THnSparse {

  public:
  HnSparseTree();
  HnSparseTree(const std::string & filename, const std::string & treename = "ndh");

  /// Setting output file name
  void SetFileName(const std::string & fn) { fFileName = fn; }
  /// Returns output filename
  std::string GetFileName() const { return fFileName; }
  /// Setting prefix path
  void SetPrefix(const std::string & prefix) { fPrefix = prefix; }
  /// Gettting prefix path
  std::string GetPrefix() const { return fPrefix; }
  /// Setting postfix path
  void SetPostfix(const std::string & postfix) { fPostfix = postfix; }
  /// Getting postfix path
  std::string GetPostfix() const { return fPostfix; }

  TFile *               GetFile() { return fFile; }
  TTree *               GetTree() { return fTree; }
  std::vector<Long64_t> GetIndexes() { return fIndexes; }
  Axes                  GetAxes() { return fAxes; }
  void                  SetAxes(Axes axes) { fAxes = axes; }
  Axis *                GetAxesAxis(int i) { return fAxes.GetAxis(i); }

  virtual void Print(Option_t * option = "") const;
  bool         InitTree(const std::string & filename = "", const std::string & treename = "ndh");
  bool         InitAxes(TObjArray * newAxes, int n = 0);
  Int_t        FillTree();
  int          FillPoints(std::vector<std::vector<int>> points);
  bool         Close(bool write = false);
  void Play(int timeout, std::vector<std::vector<int>> ranges = {{}}, std::string branches = "", int branchId = -1);
  bool AddBranch(const std::string & name, void * address, const std::string & className);
  HnSparseTreeBranch * GetBranch(const std::string & name) { return &fBranchesMap[name]; }
  void                 SetPointAt(int i, int val) { fPoint[i] = val; }
  bool                 SetFileTree(TFile * file, TTree * tree);

  void     SetBranchAddresses();
  void     EnableBranches(std::vector<std::string> branches);
  Long64_t GetEntry(Long64_t entry);

  void SaveEntry(HnSparseTree * hnstIn, std::vector<std::vector<int>> ranges, bool useProjection = false);
  // void SaveEntry(TFile * f, TTree * tree, Long64_t entry, int level);
  void AddProjectionIndexes();

  std::vector<Long64_t> GetProjectionIndexes() { return fIndexes; }

  std::map<std::string, HnSparseTreeBranch> GetBranchesMap() const { return fBranchesMap; }
  void SetBranchesMap(std::map<std::string, HnSparseTreeBranch> branchesMap) { fBranchesMap = branchesMap; }
  // Get fBranchesMap keys
  std::vector<std::string> GetBranchesMapKeys();
  static HnSparseTree *    Load(const std::string & filename, const std::string & treename = "ndh",
                                const std::string & branches = "");

  protected:
  HnSparseTree(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin = 0,
               const Double_t * xmax = 0, Int_t chunksize = 1024 * 16);
  ~HnSparseTree();

  private:
  std::string                               fFileName{"hnst.root"}; ///< Output filename
  TFile *                                   fFile{nullptr};         ///<! File
  TTree *                                   fTree{nullptr};         ///<! Container
  Int_t *                                   fPoint{nullptr};        ///<! Point
  std::vector<Long64_t>                     fIndexes;               ///<! Indexes
  std::map<std::string, HnSparseTreeBranch> fBranchesMap;           ///< Branches map
  Axes                                      fAxes;                  ///< Axes
  std::string                               fPrefix{""};            ///< Prefix path
  std::string                               fPostfix{""};           ///< Postfix path

  /// \cond CLASSIMP
  ClassDef(HnSparseTree, 1);
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
class HnSparseTreeT : public HnSparseTree {
  public:
  /// Default constructor
  HnSparseTreeT() {}
  /// Constructor
  HnSparseTreeT(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin = nullptr,
                const Double_t * xmax = nullptr, Int_t chunksize = 1024 * 16)
      : HnSparseTree(name, title, dim, nbins, xmin, xmax, chunksize)
  {
  }

  HnSparseTreeT(const std::string & filename, const std::string & treename = "ndh") : HnSparseTree(filename, treename)
  {
  }

  /// Returns content array
  TArray * GenerateArray() const override { return new CONT(GetChunkSize()); }

  private:
  /// \cond CLASSIMP
  ClassDefOverride(HnSparseTreeT, 1); // Sparse n-dimensional histogram with templated content
  /// \endcond;
};

typedef HnSparseTreeT<TArrayD> HnSparseTreeD;
typedef HnSparseTreeT<TArrayF> HnSparseTreeF;
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 32, 0)
typedef HnSparseTreeT<TArrayL64> HnSparseTreeL;
#else
typedef HnSparseT<TArrayL> HnSparseL;
#endif
typedef HnSparseTreeT<TArrayI> HnSparseTreeI;
typedef HnSparseTreeT<TArrayS> HnSparseTreeS;
typedef HnSparseTreeT<TArrayC> HnSparseTreeC;
} // namespace Ndmspc

#endif /* HNSPARSE_H */
