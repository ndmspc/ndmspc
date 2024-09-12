#include <TAxis.h>
#include <TRandom.h>

#include "ndmspc.h"
#include "HnSparse.h"

#include "HnSparseStress.h"

/// \cond CLASSIMP
ClassImp(NdmSpc::Ndh::HnSparseStress);
/// \endcond

namespace NdmSpc {
namespace Ndh {

HnSparseStress::HnSparseStress() : TObject() {}

Bool_t HnSparseStress::Generate(THnSparse * h, Long64_t size, Long64_t start)
{
  ///
  /// Generate function
  ///

  if (h == nullptr) return kFALSE;

  if (size == 0) fNFilledMax = kMaxLong64;

  fNFilledMax = size;
  if (fDebugLevel > 0)
    Printf("dimensions=%d chunkSize=%d nFillMax=%lld start=%lld", h->GetNdimensions(), h->GetChunkSize(), fNFilledMax,
           start);

  Double_t cCenter[h->GetNdimensions()];
  Int_t    cStart[h->GetNdimensions()];

  if (start > 0) {
    Long64_t allBins = 1;
    for (Int_t i = 0; i < h->GetNdimensions(); ++i) {

      if (allBins > kMaxLong64 / h->GetAxis(i)->GetNbins()) {
        Printf("Error: Product of all bins is higer then %lld !!! Do not use --start in this case !!!", kMaxLong64);
        return kFALSE;
      }
      allBins *= h->GetAxis(i)->GetNbins();
    }
    if (fDebugLevel > 0) Printf("MaxNumberOfBins=%lld", allBins);

    Long64_t startIndex = start;
    Int_t    bx         = 0;
    for (Int_t i = h->GetNdimensions() - 1; i >= 0; --i) {
      allBins /= h->GetAxis(i)->GetNbins();

      bx = startIndex / allBins;
      startIndex -= (bx * allBins);
      cStart[i] = bx;
      if (fDebugLevel > 0)
        Printf("i=%d x=%d startIndex=%lld allBins=%lld cStart[%d]=%d", i, bx, startIndex, allBins, i, cStart[i]);
    }

    if (fDebugLevel > 0)
      for (Int_t i = h->GetNdimensions() - 1; i >= 0; --i) {
        Printf("i=%d %d", i, cStart[i]);
      }
  }
  else {
    for (Int_t i = 0; i < h->GetNdimensions(); ++i) {
      cStart[i] = 0;
    }
  }

  fTimerTotal.Start();
  fTimer.Start();
  Printf("fNFilledMax=%lld filled=%lld", fNFilledMax, h->GetNbins());
  GenerateRecursiveLoop(h, h->GetNdimensions() - 1, cCenter, cStart);
  fTimer.Stop();
  fTimerTotal.Stop();
  fTimerTotal.Print("m");

  return kTRUE;
}

bool HnSparseStress::GenerateRecursiveLoop(THnSparse * h, Int_t iDim, Double_t * coord, Int_t * start)
{
  ///
  ///
  /// Generate recursive loop

  if (iDim < 0) return true;
  for (Int_t iBin = start[iDim] + 1; iBin <= h->GetAxis(iDim)->GetNbins(); iBin++) {
    coord[iDim] = h->GetAxis(iDim)->GetBinCenter(iBin);
    if (fDebugLevel > 1) Printf("iDim=%d iBin=%d center=%f", iDim, iBin, coord[iDim]);

    if (iDim == 0) {
      if (fRandomFill) {
        for (Int_t iAxis = 0; iAxis < h->GetNdimensions(); iAxis++) {
          coord[iAxis] = gRandom->Uniform(h->GetAxis(iAxis)->GetXmin(), h->GetAxis(iAxis)->GetXmax());
        }
      }
      h->Fill(coord);
      Long64_t filled = h->GetNbins();
      if (fPrintRefresh > 0 && filled % fPrintRefresh == 0)
        PrintBin(h->GetNdimensions(), coord,
                 TString::Format("%03.2f MB filled=%e [chunkSize=%e nChunks=%d]",
                                 sizeof(Double_t) * (Double_t)filled / (1024 * 1024), (Double_t)filled,
                                 (Double_t)h->GetChunkSize(), h->GetNChunks())
                     .Data());
      if (fNFilledMax > 0 && filled >= fNFilledMax) return true;
    }
    else {
      if (fDebugLevel > 1) Printf("iDim=%d", iDim);
      coord[iDim]   = h->GetAxis(iDim)->GetBinCenter(iBin);
      bool finished = GenerateRecursiveLoop(h, iDim - 1, coord, start);
      if (finished) return true;
    }

    if (iBin == h->GetAxis(iDim)->GetNbins()) start[iDim] = 0;
  }

  return false;
}

bool HnSparseStress::StressRecursiveLoop(HnSparse * h, Int_t & iDim, Int_t * coord)
{
  ///
  /// Stress recursive loop
  ///

  Long64_t nBinsSizeBytes = sizeof(Double_t) * h->GetNbins();
  if (fNBytesMax > 0 && nBinsSizeBytes > fNBytesMax) return true;

  if (iDim >= h->GetNdimensions()) return true;
  if (coord[h->GetNdimensions() - 1] > h->GetAxis(h->GetNdimensions() - 1)->GetNbins()) {
    return true;
  }

  if (nBinsSizeBytes > 0 && nBinsSizeBytes % (10 * 1024 * 1024) == 0) {
    Printf("%03.2f MB [chunks=%d binsFilled=%lld]", (Double_t)nBinsSizeBytes / (1024 * 1024), h->GetNChunks(),
           h->GetNbins());
    fTimer.Stop();
    fTimer.Print("m");
    fTimer.Reset();
    fTimer.Start();
  }

  // Printf("iDim=%d nBins=%d", iDim, GetAxis(iDim)->GetNbins());
  if (coord[iDim] < h->GetAxis(iDim)->GetNbins()) {
    coord[iDim]++;
    coord[iDim - 1] = 0;
    iDim            = 0;
    return false;
  }

  return StressRecursiveLoop(h, ++iDim, coord);
}
Bool_t HnSparseStress::Stress(HnSparse * h, Long64_t size, bool bytes)
{
  ///
  /// Stress function
  ///

  if (h == nullptr) return kFALSE;

  Long64_t nFill = size;
  if (bytes) {
    fNBytesMax = size;
    nFill      = kMaxLong64;
  }
  Printf("dimensions=%d chunkSize=%d nFill=%lld maxSize=%lld", h->GetNdimensions(), h->GetChunkSize(), nFill,
         fNBytesMax);

  Int_t    c[h->GetNdimensions()];
  Double_t cCenter[h->GetNdimensions()];
  for (Int_t i = 0; i < h->GetNdimensions(); ++i) {
    c[i] = 100;
  }

  fTimerTotal.Start();
  fTimer.Start();
  Bool_t finish = kFALSE;
  Int_t  dim;
  for (Long64_t iFill = 0; iFill < nFill; ++iFill) {
    dim    = 0;
    finish = StressRecursiveLoop(h, dim, c);
    if (finish) break;

    PrintBin(h->GetNdimensions(), cCenter, "Hello");
    h->GetBin(cCenter);
  }
  fTimer.Stop();
  fTimerTotal.Stop();
  fTimerTotal.Print("m");

  return kTRUE;
}
void HnSparseStress::PrintBin(Int_t n, Double_t * c, const char * msg)
{
  ///
  /// Print coordinates and message
  ///

  std::string s = "[";
  for (Int_t i = 0; i < n; ++i) {
    s.append(TString::Format("%.3f,", c[i]).Data());
  }
  s.resize(s.size() - 1);
  s.append("] : ");
  s.append(msg);

  Printf("%s", s.data());
}

} // namespace Ndh
} // namespace NdmSpc
