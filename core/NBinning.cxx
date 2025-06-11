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
NBinning::NBinning() : TObject()
{
  ///
  /// Default constructor
  ///
}
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
  int     nbinsMax     = 0;
  int     nContentDims = 0;
  Binning binningType;
  for (int i = 0; i < dim; i++) {
    int nbins = axes[i]->GetNbins();
    if (nbins > nbinsMax) {
      nbinsMax = nbins;
    }
    // if (nbins == 1 || axes[i]->IsAlphanumeric()) {
    if (axes[i]->IsAlphanumeric()) {
      binningType = Binning::kSingle;
      nContentDims++;
    }
    else {
      binningType = Binning::kMultiple;
      nContentDims += 3;
    }
    fBinningTypes.push_back(binningType);
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

  int        dimBinningContent   = nContentDims;
  Int_t *    nbinsBinningContent = new Int_t[dimBinningContent];
  Double_t * xminBinningContent  = new Double_t[dimBinningContent];
  Double_t * xmaxBinningContent  = new Double_t[dimBinningContent];
  int        iContentDim         = 0;
  for (int i = 0; i < dim; i++) {
    if (fBinningTypes[i] == Binning::kSingle) {
      // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
      nbinsBinningContent[iContentDim] = axes[i]->GetNbins();
      xminBinningContent[iContentDim]  = 0;
      xmaxBinningContent[iContentDim]  = axes[i]->GetNbins();
      iContentDim++;
      NLogger::Trace("[S] Binning %d: %d", i, axes[i]->GetNbins());
    }
    else if (fBinningTypes[i] == Binning::kMultiple) {
      for (int j = 0; j < 3; j++) {
        nbinsBinningContent[iContentDim + j] = axes[i]->GetNbins();
        xminBinningContent[iContentDim + j]  = 0;
        xmaxBinningContent[iContentDim + j]  = axes[i]->GetNbins();
      }
      iContentDim += 3;
      NLogger::Trace("[M] Binning %d: %d", i, axes[i]->GetNbins());
    }
    else {
      NLogger::Error("Invalid binning type %d", fBinningTypes[i]);
      return;
    }
  }
  fContent = new THnSparseI("hnstBinningContent", "hnst binning content", dimBinningContent, nbinsBinningContent,
                            xminBinningContent, xmaxBinningContent);

  int                      iContentAxis = 0;
  std::vector<std::string> types        = {"rebin", "start", "bin"};
  for (int i = 0; i < dim; i++) {
    std::string name  = axes[i]->GetName();
    std::string title = axes[i]->GetName();
    // NLogger::Trace("Binning %d: %d %d", i, axes[i]->GetNbins(), fBinningTypes[i]);
    if (fBinningTypes[i] == Binning::kSingle) {
      fContent->GetAxis(iContentAxis)->SetNameTitle(name.c_str(), title.c_str());
      iContentAxis++;
    }
    else if (fBinningTypes[i] == Binning::kMultiple) {
      for (int j = 0; j < 3; j++) {
        int         imod = j % 3;
        std::string n    = name + "_" + types[imod];
        std::string t    = title + " (" + types[imod] + ")";
        fContent->GetAxis(iContentAxis)->SetNameTitle(n.c_str(), t.c_str());
        iContentAxis++;
      }
      // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
    }
  }
  // WARN: Remove it
  // for (int i = 0; i < fContent->GetNdimensions(); i++) {
  //
  //   int         idim = i / 3;
  //   int         imod = i % 3;
  //   std::string name = axes[idim]->GetName();
  //   name += "_" + types[imod];
  //   std::string title = axes[idim]->GetName();
  //   title += " (" + types[imod] + ")";
  //
  //   fContent->GetAxis(i)->SetNameTitle(name.c_str(), title.c_str());
  // }
  //
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    NLogger::Trace("Axis[fContent] %d: %s nbins=%d", i, fContent->GetAxis(i)->GetName(),
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

  NLogger::Info("NBinning map name='%s' title='%s'", fMap->GetName(), fMap->GetTitle());
  NLogger::Info("    dimensions: %d", fMap->GetNdimensions());
  // loop over all axes and print name title
  for (int i = 0; i < fMap->GetNdimensions(); i++) {
    NLogger::Info("    [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, fMap->GetAxis(i)->GetName(),
                  fMap->GetAxis(i)->GetTitle(), fMap->GetAxis(i)->GetNbins(), fMap->GetAxis(i)->GetXmin(),
                  fMap->GetAxis(i)->GetXmax());
  }
  NLogger::Info("    filled bins = %lld", fMap->GetNbins());
  NLogger::Info("NBinning content name='%s' title='%s'", fContent->GetName(), fContent->GetTitle());
  NLogger::Info("    dimensions: %d", fContent->GetNdimensions());
  // loop over all axes and print name title
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    NLogger::Info("    [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, fContent->GetAxis(i)->GetName(),
                  fContent->GetAxis(i)->GetTitle(), fContent->GetAxis(i)->GetNbins(), fContent->GetAxis(i)->GetXmin(),
                  fContent->GetAxis(i)->GetXmax());
  }
  NLogger::Info("    filled bins = %lld", fContent->GetNbins());
  PrintContent(option);
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
    // NLogger::Trace("Bin %lld: %f %s", linBin, v, binCoords.c_str());

    int  iAxis   = 0;
    bool isValid = false;
    // for (int i = 0; i < cSparse->GetNdimensions(); i += 3) {
    int index = 0;
    for (iAxis = 0; iAxis < fAxes.size(); iAxis++) {
      int min;
      int max;
      // Print type of binning
      // NLogger::Trace("Axis %d: %s [%s]", iAxis, fAxes[iAxis]->GetName(),
      //                fBinningTypes[iAxis] == Binning::kSingle ? "S" : "M");
      if (fBinningTypes[iAxis] == Binning::kSingle) {
        isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], 1, 1, bins[index], min, max);
        binCoords += TString::Format(" | (S) %s %d %d %d [%d,%d] [%d,%d]", fAxes[iAxis]->GetName(), 1, 1, bins[index],
                                     min, max, 1, fAxes[iAxis]->GetNbins())
                         .Data();
        index++;
      }
      else if (fBinningTypes[iAxis] == Binning::kMultiple) {
        isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], bins[index], bins[index + 1], bins[index + 2], min, max);
        binCoords += TString::Format(" | (M) %s %d %d %d [%d,%d] [%d,%d]", fAxes[iAxis]->GetName(), bins[index],
                                     bins[index + 1], bins[index + 2], min, max, 1, fAxes[iAxis]->GetNbins())
                         .Data();
        index += 3;
      }
      else {
        NLogger::Error("Unknown binning type");
        continue;
      }
      if (!isValid) {
        NLogger::Error("Cannot get axis range for axis %d", iAxis);
        continue;
      }
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

  //  fContent->Reset();

  std::vector<int>                           mins(fMap->GetAxis(0)->GetNbins(), 1);
  std::vector<int>                           maxs(fMap->GetAxis(0)->GetNbins(), 0);
  std::vector<std::vector<std::vector<int>>> content(fMap->GetAxis(0)->GetNbins());

  // loop over all selected bins via ROOT iterarot for THnSparse
  THnSparse *                                     cSparse = fMap;
  Int_t *                                         p       = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  // Print number of filled bins
  // NLogger::Info("Filled bins = %lld", cSparse->GetNbins());
  while ((linBin = iter->Next()) >= 0) {
    Double_t v = cSparse->GetBinContent(linBin, p);
    // continue;
    int idx = p[0] - 1;
    NLogger::Trace("Bin %lld: %d %d %d %d type=%d", linBin, p[0], p[1], p[2], p[3], fBinningTypes[idx]);
    if (fBinningTypes[idx] == Binning::kSingle) {
      content[idx].push_back({p[0], p[3]});
    }
    else {
      // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
      content[idx].push_back({p[0], p[1], p[2], p[3]});
    }
    maxs[idx] = maxs[idx] + 1;
  }
  delete[] p;

  // NLogger::Debug("Filling All ...");
  // Loop over all binning combinations
  NDimensionalExecutor executor(mins, maxs);
  auto                 binning_task = [&content, &nBinsFilled, this](const std::vector<int> & coords) {
    std::vector<int> pointContentVector;
    int              iContentpoint = 0;
    // NLogger::Debug("Binning task: %s", NUtils::GetCoordsString(coords, -1).c_str());
    for (size_t i = 0; i < coords.size(); i++) {
      if (content[i][coords[i] - 1].size() == 2) {
        pointContentVector.push_back(content[i][coords[i] - 1][1]);
      }
      else {
        pointContentVector.push_back(content[i][coords[i] - 1][1]);
        pointContentVector.push_back(content[i][coords[i] - 1][2]);
        pointContentVector.push_back(content[i][coords[i] - 1][3]);
      }
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

  fMap->Reset();

  return nBinsFilled;
}

bool NBinning::AddBinning(int id, std::vector<int> binning, int n)
{
  ///
  /// Add binning
  ///

  if (fAxes.size() == 0) {
    NLogger::Error("AddBinning: No axes defined !!!");
    return false;
  }

  if (id <= 0 || id > fAxes.size()) {
    NLogger::Error("AddBinning: Invalid binning id %d", id);
    return false;
  }

  binning.insert(binning.begin(), id);
  Int_t * point = new Int_t[fMap->GetNdimensions()];
  NUtils::VectorToArray(binning, point);
  if (binning.size() == 2) {
    point[3] = point[1];
    point[2] = 1;
    point[1] = 1;
  }
  for (int i = 0; i < n; i++) {
    NLogger::Debug("Adding binning %d: %d %d %d %d", i, point[0], point[1], point[2], point[3]);
    fMap->SetBinContent(point, 1);
    point[3] += 1;
  }
  delete[] point;

  return true;
}
bool NBinning::AddBinningVariable(int id, std::vector<int> mins)
{
  ///
  /// Add Variable binning
  ///
  if (id <= 0 || id > fAxes.size()) {
    NLogger::Error("AddBinningVariable: Invalid binning id %d", id);
    return false;
  }

  // lopp over all mins and set binning
  for (int i = 1; i < mins.size(); i++) {

    if (mins[i] < 1 || mins[i] > fAxes[id - 1]->GetNbins() + 1) {
      NLogger::Warning("AddBinningVariable: Invalid binning value mins=%d", mins[i]);
      return true;
    }
    // if (mins[i] == fAxes[id - 1]->GetNbins() || i == mins.size() - 1) mins[i]++;
    int rebin = mins[i] - mins[i - 1];
    if (rebin < 1) {
      NLogger::Error("AddBinningVariable: Invalid binning value rebin=%d", rebin);
      return false;
    }
    int rebin_start = mins[i - 1] % rebin;
    int bin         = mins[i] / rebin;
    if (rebin == 1) {
      rebin_start = 1;
      bin         = mins[i - 1];
    }
    NLogger::Debug("Adding partial binning %d: %d %d %d %d", i - 1, id, rebin, rebin_start, bin);
    AddBinning(id, {rebin, rebin_start, bin}, 1);
  }

  return true;
}
bool NBinning::AddBinningViaBinWidths(int id, std::vector<std::vector<int>> widths)
{
  ///
  /// Add binning via bin widths
  ///
  NLogger::Debug("Adding binning via bin widths for id=%d", id);
  std::vector<int> mins;
  // Print vector of widths
  mins.push_back(1);
  for (auto & w : widths) {
    NLogger::Debug("Adding binning via bin widths: %s", NUtils::GetCoordsString(w, -1).c_str());
    int width   = w[0];
    int nWidths = w.size() > 1 ? w[1] : 1;
    for (int iWidth = 0; iWidth < nWidths; iWidth++) {
      mins.push_back(mins[mins.size() - 1] + width);
    }
  }

  return AddBinningVariable(id, mins);
}

std::vector<std::vector<int>> NBinning::GetCoordsRange(std::vector<int> c) const
{
  std::vector<std::vector<int>> coordsRange;
  std::vector<int>              ids;
  std::vector<int>              mins;
  std::vector<int>              maxs;
  int                           iAxis = 0;
  bool                          isValid;
  int                           index = 0;
  for (int iAxis = 0; iAxis < fAxes.size(); iAxis++) {
    // for (int i = 0; i < fContent->GetNdimensions(); i += 3) {
    int min;
    int max;
    if (fBinningTypes[iAxis] == Binning::kSingle) {
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], 1, 1, c[index], min, max);
      index++;
    }
    else if (fBinningTypes[iAxis] == Binning::kMultiple) {
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], c[index], c[index + 1], c[index + 2], min, max);
      index += 3;
    }
    else {
      NLogger::Error("Unknown binning type");
      continue;
    }
    if (!isValid) {
      // NLogger::Error("Cannot get axis range for axis %d", iAxis);
      return {};
    }
    // print min max
    NLogger::Debug("Axis %d: %s [%d,%d]", iAxis, fAxes[iAxis]->GetName(), min, max);
    ids.push_back(iAxis); // +1 because id starts from 1
    mins.push_back(min);
    maxs.push_back(max);
  }

  coordsRange.push_back(ids);
  coordsRange.push_back(mins);
  coordsRange.push_back(maxs);
  return coordsRange;
}

Binning NBinning::GetBinningType(int i) const
{
  if (i < 0 || i >= fBinningTypes.size()) {
    NLogger::Error("Invalid binning type %d", i);
    return Binning::kSingle;
  }
  return fBinningTypes[i];
}
std::vector<std::vector<int>> NBinning::GetAxisRanges(std::vector<int> c) const
{
  ///
  /// Returns axis ranges for given coordinates
  ///
  std::vector<std::vector<int>> axisRanges;

  int  iAxis = 0;
  bool isValid;
  int  index = 0;
  for (int iAxis = 0; iAxis < fAxes.size(); iAxis++) {
    // for (int i = 0; i < fContent->GetNdimensions(); i += 3) {
    int min;
    int max;
    if (fBinningTypes[iAxis] == Binning::kSingle) {
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], 1, 1, c[index], min, max);
      index++;
    }
    else if (fBinningTypes[iAxis] == Binning::kMultiple) {
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], c[index], c[index + 1], c[index + 2], min, max);
      index += 3;
    }
    else {
      NLogger::Error("Unknown binning type");
      continue;
    }
    if (!isValid) {
      // NLogger::Error("Cannot get axis range for axis %d", iAxis);
      return {};
    }
    // print min max
    NLogger::Trace("Axis %d: %s [%d,%d]", iAxis, fAxes[iAxis]->GetName(), min, max);
    axisRanges.push_back({iAxis, min, max});
  }

  return axisRanges;
}

} // namespace Ndmspc
