#include "NDimensionalExecutor.h"
#include "NUtils.h"
#include "NLogger.h"
#include "NThreadData.h"
#include "RtypesCore.h"
#include "NBinning.h"
#include <string>
#include <vector>

/// \cond CLASSIMP
ClassImp(Ndmspc::NBinning);
/// \endcond

namespace Ndmspc {
NBinning::NBinning(std::vector<TAxis *> axes) : TObject(), fAxes(axes)
{
  ///
  /// Constructor
  ///

  if (axes.size() == 0) {
    NLogger::Error("No axes provided");
    return;
  }

  int dim = axes.size();
  // Calculate maximyn=m of nbins
  int nbinsMax = 0;
  for (int i = 0; i < dim; i++) {
    int nbins = axes[i]->GetNbins();
    if (nbins > nbinsMax) {
      nbinsMax = nbins;
    }
  }
  int        dimBinning   = 4;
  Int_t *    nbinsBinning = new Int_t[dimBinning];
  Double_t * xminBinning  = new Double_t[dimBinning];
  Double_t * xmaxBinning  = new Double_t[dimBinning];
  // set first item nbinsBinning to dim min to zero and max to dim

  nbinsBinning[0] = dim;
  xminBinning[0]  = 0;
  xmaxBinning[0]  = dim;
  for (int i = 1; i < dimBinning; i++) {
    nbinsBinning[i] = nbinsMax;
    xminBinning[i]  = 0;
    xmaxBinning[i]  = nbinsMax;
  }

  if (fMap) {
    delete fMap;
  }
  fMap = new THnSparseI("hnstBinnings", "hnst binnings", dimBinning, nbinsBinning, xminBinning, xmaxBinning);
  if (fMap == nullptr) {
    NLogger::Error("Cannot create binnings !!!");
    return;
  }
  fMap->GetAxis(0)->SetNameTitle("dim", "dimension");
  fMap->GetAxis(1)->SetNameTitle("rebin", "rebins");
  fMap->GetAxis(2)->SetNameTitle("start", "rebins start");
  fMap->GetAxis(3)->SetNameTitle("bin", "bin id");

  for (int i = 0; i < dim; i++) {
    fMap->GetAxis(0)->SetBinLabel(i + 1, axes[i]->GetName());
  }

  int        dimBinningContent   = 3 * dim; //
  Int_t *    nbinsBinningContent = new Int_t[dimBinningContent];
  Double_t * xminBinningContent  = new Double_t[dimBinningContent];
  Double_t * xmaxBinningContent  = new Double_t[dimBinningContent];

  for (int i = 0; i < dimBinningContent; i++) {
    int idim = i / 3;
    // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
    nbinsBinningContent[i] = axes[idim]->GetNbins();
    xminBinningContent[i]  = 0;
    xmaxBinningContent[i]  = axes[idim]->GetNbins();
  }
  fContent = new THnSparseI("hnstBinningContent", "hnst binning content", dimBinningContent, nbinsBinningContent,
                            xminBinningContent, xmaxBinningContent);

  std::vector<std::string> types = {"rebin", "start", "bin"};
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    int         idim = i / 3;
    int         imod = i % 3;
    std::string name = axes[idim]->GetName();
    name += "_" + types[imod];
    std::string title = axes[idim]->GetName();
    title += " (" + types[imod] + ")";

    fContent->GetAxis(i)->SetNameTitle(name.c_str(), title.c_str());
  }
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    NLogger::Debug("Axis[fContent] %d: %s nbins=%d", i, fContent->GetAxis(i)->GetName(),
                   fContent->GetAxis(i)->GetNbins());
  }

  delete[] nbinsBinning;
  delete[] xminBinning;
  delete[] xmaxBinning;
}
NBinning::~NBinning()
{
  ///
  /// Destructor
  ///
}
void NBinning::Print(Option_t * option) const
{
  ///
  /// Print
  ///

  if (fMap == nullptr) {
    NLogger::Error("THnSparse fMap is null");
    return;
  }

  NLogger::Info("NBinning name='%s' title='%s'", fMap->GetName(), fMap->GetTitle());
  NLogger::Info("THnSparse:");
  NLogger::Info("  dimensions: %d", fMap->GetNdimensions());
  NLogger::Info("  filled bins = %lld", fMap->GetNbins());
  // fMap->Print(option);

  // // Using executor print all bins
  // NDimensionalExecutor executor(fMap, true);
  // // NDimensionalExecutor executor(fMap, false);
  // auto                          binning_task = [this, &all_bins](const std::vector<int> & coords) {
  //   fMap->Print();
  //   NUtils::PrintPointSafe(coords, -1);
  // };
  //
  // executor.Execute(binning_task);

  // size_t num_threads_to_use = 0;
  // // set num_threads_to_use to the number of available threads
  // if (num_threads_to_use < 1) num_threads_to_use = std::thread::hardware_concurrency();
  // NLogger::Info("Using %zu threads ...", num_threads_to_use);
  // std::vector<NThreadData> thread_data_vector(num_threads_to_use);
  // for (size_t i = 0; i < thread_data_vector.size(); ++i) {
  //   thread_data_vector[i].SetAssignedIndex(i);
  // }
  //
  // // 2. Define the lambda (type in signature updated)
  // auto parallel_task = [](const std::vector<int> & coords, NThreadData & thread_obj) {
  //   NUtils::PrintPointSafe(coords, thread_obj.GetAssignedIndex());
  //   thread_obj.Process(coords);
  //   // thread_obj.Print();
  // };
  //
  // // 3. Call ExecuteParallel (template argument updated)
  // executor.ExecuteParallel<NThreadData>(parallel_task, thread_data_vector);
}
void NBinning::PrintContent(Option_t * option) const
{
  ///
  /// Print content
  ///

  TString opt(option);

  // loop over all selected bins via ROOT iterarot for THnSparse
  THnSparse *                                     cSparse = fContent;
  Int_t *                                         bins    = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t    v         = cSparse->GetBinContent(linBin, bins);
    Long64_t    idx       = cSparse->GetBin(bins);
    std::string binCoords = NUtils::GetCoordsString(NUtils::ArrayToVector(bins, cSparse->GetNdimensions()), -1);

    int  iAxis   = 0;
    bool isValid = false;
    for (int i = 0; i < cSparse->GetNdimensions(); i += 3) {
      int min;
      int max;
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], bins[i], bins[i + 1], bins[i + 2], min, max);
      if (!isValid) {
        // NLogger::Error("Cannot get axis range for axis %d", iAxis);
        continue;
      }
      binCoords += TString::Format("| %s %d %d %d [%d,%d] [%d,%d]", fAxes[iAxis]->GetName(), bins[i], bins[i + 1],
                                   bins[i + 2], min, max, 1, fAxes[iAxis]->GetNbins())
                       .Data();
      iAxis++;
    }
    if (!isValid) {
      // NLogger::Error("Cannot get axis range for axis %d", iAxis);
      NLogger::Error("Bin %lld: %s  [not valid for axis=%s]", linBin, binCoords.c_str(), fAxes[iAxis]->GetName());
      continue;
    }
    NLogger::Debug("Bin %lld: %s", linBin, binCoords.c_str());
  }
  delete[] bins;
}

int NBinning::FillAll()
{
  ///
  /// Fill content binnings from mapping
  ///
  int nBinsFilled = 0;

  fContent->Reset();

  std::vector<int>                           mins(fMap->GetNdimensions(), 1);
  std::vector<int>                           maxs(fMap->GetNdimensions(), 0);
  std::vector<std::vector<std::vector<int>>> content(fMap->GetNdimensions());

  // loop over all selected bins via ROOT iterarot for THnSparse
  THnSparse *                                     cSparse = fMap;
  Int_t *                                         p       = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t v   = cSparse->GetBinContent(linBin, p);
    int      idx = p[0] - 1;
    content[idx].push_back({p[0], p[1], p[2], p[3]});
    maxs[idx] = maxs[idx] + 1;
  }
  delete[] p;
  // loop over all bins
  // for (Long64_t i = 0; i < fMap->GetNbins(); i++) {
  //   Int_t p[fMap->GetNdimensions()];
  //   fMap->GetBinContent(i, p);
  //   // NLogger::Debug("Bin %lld: ", p[0]);
  //   int idx = p[0] - 1;
  //   content[idx].push_back({p[0], p[1], p[2], p[3]});
  //   maxs[idx] = maxs[idx] + 1;
  // }

  /// /// Print all mins and maxs
  /// for (size_t i = 0; i < mins.size(); i++) {
  ///   NLogger::Info("Min[%zu]: %d", i, mins[i]);
  ///   NLogger::Info("Max[%zu]: %d", i, maxs[i]);
  /// }
  // Loop over all binning combinations
  NDimensionalExecutor executor(mins, maxs);
  auto                 binning_task = [&content, &nBinsFilled, this](const std::vector<int> & coords) {
    std::vector<int> pointContentVector;
    int              iContentpoint = 0;
    for (size_t i = 0; i < coords.size(); i++) {
      pointContentVector.push_back(content[i][coords[i] - 1][1]);
      pointContentVector.push_back(content[i][coords[i] - 1][2]);
      pointContentVector.push_back(content[i][coords[i] - 1][3]);
    }

    // NUtils::PrintPointSafe(pointContentVector, -1);
    Int_t nContentDims = fContent->GetNdimensions();
    Int_t pointContent[nContentDims];
    NUtils::VectorToArray(pointContentVector, pointContent);
    Long64_t pointContentBin = fContent->GetBin(pointContent);
    fContent->SetBinContent(pointContentBin, 1);
    nBinsFilled++;
  };
  executor.Execute(binning_task);

  return nBinsFilled;
}

std::vector<std::vector<int>> NBinning::GetAll()
{

  NLogger::Debug("entries=%lld", fContent->GetEntries());
  std::vector<std::vector<int>> all_bins;
  /// set minimum and maximum boundaries
  std::vector<int>                           mins(fMap->GetNdimensions(), 1);
  std::vector<int>                           maxs(fMap->GetNdimensions(), 0);
  std::vector<std::vector<std::vector<int>>> content(fMap->GetNdimensions());

  // loop over all bins
  for (Long64_t i = 0; i < fMap->GetNbins(); i++) {
    Int_t p[fMap->GetNdimensions()];
    fMap->GetBinContent(i, p);
    // NLogger::Debug("Bin %lld: ", p[0]);
    int idx = p[0] - 1;
    content[idx].push_back({p[0], p[1], p[2], p[3]});
    maxs[idx] = maxs[idx] + 1;
  }

  /// /// Print all mins and maxs
  /// for (size_t i = 0; i < mins.size(); i++) {
  ///   NLogger::Info("Min[%zu]: %d", i, mins[i]);
  ///   NLogger::Info("Max[%zu]: %d", i, maxs[i]);
  /// }
  // Loop over all binning combinations
  NDimensionalExecutor executor(mins, maxs);
  auto                 binning_task = [&content, &all_bins, this](const std::vector<int> & coords) {
    all_bins.push_back(coords);
    std::vector<int> pointContentVector;
    int              iContentpoint = 0;
    for (size_t i = 0; i < coords.size(); i++) {
      pointContentVector.push_back(content[i][coords[i] - 1][1]);
      pointContentVector.push_back(content[i][coords[i] - 1][2]);
      pointContentVector.push_back(content[i][coords[i] - 1][3]);
    }

    // NUtils::PrintPointSafe(pointContentVector, -1);
    Int_t nContentDims = fContent->GetNdimensions();
    Int_t pointContent[nContentDims];
    NUtils::VectorToArray(pointContentVector, pointContent);
    // NLogger::Debug("%d %d %d %d %d %d %d %d %d %d %d %d", pointContent[0], pointContent[1], pointContent[2],
    //                                pointContent[3], pointContent[4], pointContent[5], pointContent[6],
    //                                pointContent[7], pointContent[8], pointContent[9], pointContent[10],
    //                                pointContent[11]);
    // Print linear bin
    // print number of entries
    // NLogger::Debug("nBins=%lld", fContent->GetNbins());
    // if (fContent->GetNbins() < 225) {
    Long64_t pointContentBin = fContent->GetBin(pointContent);
    // NLogger::Debug("Point content: %lld", pointContentBin);
    // NUtils::PrintPointSafe(pointContentVector, -1);
    fContent->SetBinContent(pointContentBin, 1);
    // }

    // NUtils::PrintPointSafe(coords, -1);
    // std::string coords_str = "[";
    // for (size_t i = 0; i < coords.size(); i++) {
    //   coords_str += NUtils::GetCoordsString(content[i][coords[i] - 1], -1);
    //   coords_str += (i == coords.size() - 1 ? "" : ",");
    // }
    // coords_str += "]";
    // NLogger::Info("content: %s", coords_str.c_str());
  };
  executor.Execute(binning_task);

  // for (size_t i = 0; i < all_bins.size(); i++) {
  //   NUtils::PrintPointSafe(all_bins[i], i);
  // }

  return all_bins;
}
bool NBinning::AddBinning(std::vector<int> binning, int n)
{
  ///
  /// Add binning
  ///

  if (fAxes.size() == 0) {
    NLogger::Error("AddBinning: No axes defined !!!");
    return false;
  }

  Int_t * point = new Int_t[binning.size()];
  for (int i = 0; i < n; i++) {
    NUtils::VectorToArray(binning, point);
    fMap->SetBinContent(point, 1);
  }

  return true;
}

} // namespace Ndmspc
