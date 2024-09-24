#include <TFile.h>
#include <TTree.h>
#include <TAxis.h>
#include <TH2D.h>
#include <THnSparse.h>

#include "HnSparse.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::Ndh::HnSparse);
/// \endcond

namespace NdmSpc {
namespace Ndh {

HnSparse::HnSparse() : THnSparse()
{
  ///
  /// Default constructor
  ///
}

HnSparse::HnSparse(const char * name, const char * title, Int_t dim, const Int_t * nbins, const Double_t * xmin,
                   const Double_t * xmax, Int_t chunksize)
    : THnSparse(name, title, dim, nbins, xmin, xmax, chunksize)
{
  ///
  /// Constructor
  ///
}

Bool_t HnSparse::Import(std::vector<Int_t> r, TString filename, TString objname, TString cacheDir)
{
  ///
  /// Import THnSparse from file
  ///

  if (!cacheDir.IsNull()) TFile::SetCacheFileDir(cacheDir.Data(), 1, 1);

  if (filename.IsNull()) {
    Printf("Error: filename is empty !!!");
    return kFALSE;
  }
  if (objname.IsNull()) {
    Printf("Error: objname is empty !!!");
    return kFALSE;
  }

  Printf("Opening file='%s' obj='%s' ...", filename.Data(), objname.Data());
  TFile * f = TFile::Open(filename.Data());
  if (f == nullptr) return kFALSE;

  THnSparse * s = (THnSparse *)f->Get(objname.Data());

  // s->Print();

  TAxis * a;

  TObjArray * newAxis = (TObjArray *)s->GetListOfAxes()->Clone();
  for (Int_t iDim = 0; iDim < newAxis->GetEntries(); iDim++) {
    a = (TAxis *)newAxis->At(iDim);
    // Printf("%s %d %.2f %.2f", a->GetName(), a->GetNbins(), a->GetXmin(), a->GetXmax());
    // a->Print();
    if (std::find(r.begin(), r.end(), iDim) != r.end()) {
      // Printf("%s %d %.2f %.2f", a->GetName(), a->GetNbins(), a->GetXmin(), a->GetXmax());
    }
    else {
      /* v does not contain x */
      // Printf("Reset %s %d %.2f %.2f", a->GetName(), a->GetNbins(), a->GetXmin(), a->GetXmax());
      a->Set(1, a->GetXmin(), a->GetXmax());
    }
  }

  Init(TString::Format("NDMSPC_%s", s->GetName()).Data(), "", newAxis, kTRUE);

  Int_t dims[fNdimensions];
  Int_t c[fNdimensions];
  for (Int_t iDim = 0; iDim < newAxis->GetEntries(); iDim++) {
    // a = s->GetAxis(iDim);
    // Printf("%s %d %.2f %.2f", a->GetName(), a->GetNbins(), a->GetXmin(), a->GetXmax());
    dims[iDim] = iDim;
    c[iDim]    = 1;
  }
  TFile * fileOut = TFile::Open(fOutputFileName.Data(), "RECREATE");
  fTree           = new TTree("ndh", "Ndh tree");
  fTree->Branch("h", &s);

  RecursiveLoop(s, 0, c, dims, r);

  // Reset cuts
  for (Int_t iDim = 0; iDim < GetNdimensions(); iDim++) {
    GetAxis(iDim)->SetRange();
  }

  fTree->GetUserInfo()->Add(Clone());
  fTree->Write();
  fileOut->Close();

  delete s;
  f->Close();

  return kTRUE;
}

bool HnSparse::RecursiveLoop(THnSparse * s, Int_t level, Int_t * coord, Int_t * dims, std::vector<Int_t> & r)
{
  ///
  /// Recursive loop
  ///

  if (level >= (Int_t)r.size()) return true;

  // Printf("level=%d axis_id=%d", level, r[level]);

  for (Int_t iBin = 1; iBin <= GetAxis(r[level])->GetNbins(); iBin++) {

    coord[r[level]] = iBin;
    s->GetAxis(r[level])->SetRange(iBin, iBin);
    Bool_t finished = RecursiveLoop(s, level + 1, coord, dims, r);
    if (finished) {
      THnSparse * ss = (THnSparse *)s->ProjectionND(s->GetNdimensions(), dims, "O");

      ss->SetName(GetName());
      ss->SetEntries(1);
      // GetBin(coord, kTRUE);
      if (ss->GetNbins() > 0) {
        SetBinContent(coord, ss->GetNbins());
        Printf("level=%d axis_id=%d iBin=%d binsFilled=%lld", level, r[level], iBin, ss->GetNbins());
        fTree->SetBranchAddress("h", &ss);
        fTree->Fill();
      }
      else {
        Printf("[NotFilled] level=%d axis_id=%d iBin=%d binsFilled=%lld", level, r[level], iBin, ss->GetNbins());
      }
      delete ss;
    }
    else {
      Printf("level=%d axis_id=%d iBin=%d", level, r[level], iBin);
    }
  }

  return false;
}

void HnSparse::ReserveBins(Long64_t nBins)
{
  ///
  /// Reserve N bins
  ///
  Printf("Reserving %e bins ...", (Double_t)nBins);
  Reserve(nBins);
  Printf("%e bins reserved.", (Double_t)nBins);
}
} // namespace Ndh
} // namespace NdmSpc
