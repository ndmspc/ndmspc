#ifndef HnSparse_H
#define HnSparse_H

#include <TObject.h>
#include <TSystem.h>
#include <TTree.h>
#include <THnSparse.h>

namespace NDH {

///
/// \class HnSparse
///
/// \brief HnSparse object
///	\author Martin Vala <mvala@cern.ch>
///

class HnSparse : public THnSparse {

  protected:
  HnSparse();
  HnSparse(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin = 0,
           const Double_t * xmax = 0, Int_t chunksize = 1024 * 16);

  public:
  Bool_t Import(std::vector<Int_t> r, TString filename, TString objname, TString cacheDir = gSystem->HomeDirectory());

  /// Setting output file name
  void SetOutputFileName(const char * fn) { fOutputFileName = fn; }
  /// Returns output filename
  TString GetOutputFileName() const { return fOutputFileName; }

  void ReserveBins(Long64_t nBins);

  protected:
  bool RecursiveLoop(THnSparse * s, Int_t level, Int_t * coord, Int_t * dims, std::vector<Int_t> & r);

  private:
  TTree * fTree{nullptr};              ///< Container
  TString fOutputFileName{"ndh.root"}; ///< Output filename

  /// \cond CLASSIMP
  ClassDef(HnSparse, 1);
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
class HnSparseT : public HnSparse {
  public:
  HnSparseT() {}
  HnSparseT(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin = nullptr,
            const Double_t * xmax = nullptr, Int_t chunksize = 1024 * 16)
      : HnSparse(name, title, dim, nbins, xmin, xmax, chunksize)
  {
  }

  TArray * GenerateArray() const override { return new CONT(GetChunkSize()); }

  private:
  ClassDefOverride(HnSparseT, 1); // Sparse n-dimensional histogram with templated content
};

typedef HnSparseT<TArrayD> HnSparseD;
typedef HnSparseT<TArrayF> HnSparseF;
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 32, 0)
typedef HnSparseT<TArrayL64> HnSparseL;
#else
typedef HnSparseT<TArrayL> HnSparseL;
#endif
typedef HnSparseT<TArrayI> HnSparseI;
typedef HnSparseT<TArrayS> HnSparseS;
typedef HnSparseT<TArrayC> HnSparseC;

} // namespace NDH

#endif /* HNSPARSE_H */
