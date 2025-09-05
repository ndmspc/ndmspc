#include <string>
#include <vector>
#include <TAttAxis.h>
#include <TObjArray.h>
#include "NBinningDef.h"
#include "NBinningPoint.h"
#include "NDimensionalExecutor.h"
#include "NUtils.h"
#include "NLogger.h"
#include "RtypesCore.h"
#include "NBinning.h"

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

  Initialize();
}
NBinning::NBinning(TObjArray * axes) : TObject()
{
  ///
  /// Constructor
  ///

  if (axes == nullptr) {
    NLogger::Error("NBinning(TObjArray * axes) : axes is nullptr");
    return;
  }

  for (int i = 0; i < axes->GetEntriesFast(); i++) {
    TAxis * axis = dynamic_cast<TAxis *>(axes->At(i));
    if (axis) {
      fAxes.push_back(axis);
    }
    else {
      NLogger::Error("NBinning: Axis %d is not a TAxis", i);
    }
  }

  Initialize();
}
NBinning::~NBinning()
{
  ///
  /// Destructor
  ///

  // Cleanup fDefinitions
  for (auto & def : fDefinitions) {
    delete def.second;
  }
  fDefinitions.clear();

  // Cleanup fAxes
  for (auto & axis : fAxes) {
    if (axis) {
      delete axis; // Delete the axis if it was created by this class
    }
  }

  delete fMap;
  delete fContent;
}

void NBinning::Initialize()
{
  ///
  /// Initialize binning
  ///

  if (fAxes.size() == 0) {
    NLogger::Error("No axes provided");
    return;
  }

  int dim = fAxes.size();
  // Calculate maximyn=m of nbins
  int     nbinsMax     = 0;
  int     nContentDims = 0;
  Binning binningType;
  for (int i = 0; i < dim; i++) {
    int nbins = fAxes[i]->GetNbins();
    if (nbins > nbinsMax) {
      nbinsMax = nbins;
    }
    // if (nbins == 1 || axes[i]->IsAlphanumeric()) {
    TString axisname(fAxes[i]->GetName());
    bool    isUser = axisname.Contains("/U");
    if (isUser) {
      axisname.ReplaceAll("/U", "");
      fAxes[i]->SetName(axisname.Data());
    }

    if (isUser) {
      binningType = Binning::kUser;
      nContentDims++;
    }
    else if (fAxes[i]->IsAlphanumeric()) {
      binningType = Binning::kSingle;
      nContentDims++;
    }
    else {
      binningType = Binning::kMultiple;
      nContentDims += 3;
    }
    fBinningTypes.push_back(binningType);
    fAxisTypes.push_back(AxisType::kFixed);
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
    fMap->GetAxis(0)->SetBinLabel(i + 1, fAxes[i]->GetName());
  }

  int        dimBinningContent   = nContentDims;
  Int_t *    nbinsBinningContent = new Int_t[dimBinningContent];
  Double_t * xminBinningContent  = new Double_t[dimBinningContent];
  Double_t * xmaxBinningContent  = new Double_t[dimBinningContent];
  int        iContentDim         = 0;
  for (int i = 0; i < dim; i++) {
    if (fBinningTypes[i] == Binning::kSingle) {
      // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
      nbinsBinningContent[iContentDim] = fAxes[i]->GetNbins();
      xminBinningContent[iContentDim]  = 0;
      xmaxBinningContent[iContentDim]  = fAxes[i]->GetNbins();
      iContentDim++;
      NLogger::Trace("[S] Binning %d: %d", i, fAxes[i]->GetNbins());
    }
    else if (fBinningTypes[i] == Binning::kMultiple) {
      for (int j = 0; j < 3; j++) {
        nbinsBinningContent[iContentDim + j] = fAxes[i]->GetNbins();
        xminBinningContent[iContentDim + j]  = 0;
        xmaxBinningContent[iContentDim + j]  = fAxes[i]->GetNbins();
      }
      iContentDim += 3;
      NLogger::Trace("[M] Binning %d: %d", i, fAxes[i]->GetNbins());
    }
    else if (fBinningTypes[i] == Binning::kUser) {
      // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
      nbinsBinningContent[iContentDim] = fAxes[i]->GetNbins();
      xminBinningContent[iContentDim]  = 0;
      xmaxBinningContent[iContentDim]  = fAxes[i]->GetNbins();
      iContentDim++;
      NLogger::Trace("[U] Binning %d: %d", i, fAxes[i]->GetNbins());
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
    std::string name  = fAxes[i]->GetName();
    std::string title = fAxes[i]->GetName();
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
    else if (fBinningTypes[i] == Binning::kUser) {
      fContent->GetAxis(iContentAxis)->SetNameTitle(name.c_str(), title.c_str());
      iContentAxis++;
    }
  }
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    NLogger::Trace("Axis[fContent] %d: %s nbins=%d", i, fContent->GetAxis(i)->GetName(),
                   fContent->GetAxis(i)->GetNbins());
  }

  fPoint = new NBinningPoint(this);

  delete[] nbinsBinning;
  delete[] xminBinning;
  delete[] xmaxBinning;
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

  TString opt(option);
  opt.ToLower();

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
  // print list of definitions names from fDefinitions
  NLogger::Info("NBinning definitions:");
  for (const auto & kv : fDefinitions) {
    if (kv.first == fCurrentDefinitionName) {
      NLogger::Info("  '%s' (current)", kv.first.c_str());
    }
    else {
      NLogger::Info("  '%s'", kv.first.c_str());
    }
  }

  if (option && opt.Contains("all")) {
    NLogger::Info("NBinning content:");
    PrintContent(option);
  }

  // // loop over definition and print
  // for (const auto & kv : fDefinition) {
  //   NLogger::Info("Binning '%s':", kv.first.c_str());
  //   for (const auto & v : kv.second) {
  //     NLogger::Info("  %s", NUtils::GetCoordsString(v, -1).c_str());
  //   }
  // }

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

  std::map<std::string, std::vector<int>> binningMap;

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
        binCoords += TString::Format(" | (S%c) %s %d %d %d [%d,%d] [%d,%d]", GetAxisTypeChar(iAxis),
                                     fAxes[iAxis]->GetName(), 1, 1, bins[index], min, max, 1, fAxes[iAxis]->GetNbins())
                         .Data();
        binningMap[fAxes[iAxis]->GetName()] = {1, 1, bins[index]};
        index++;
      }
      else if (fBinningTypes[iAxis] == Binning::kMultiple) {
        isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], bins[index], bins[index + 1], bins[index + 2], min, max);
        binCoords +=
            TString::Format(" | (M%c) %s %d %d %d [%d,%d] [%d,%d]", GetAxisTypeChar(iAxis), fAxes[iAxis]->GetName(),
                            bins[index], bins[index + 1], bins[index + 2], min, max, 1, fAxes[iAxis]->GetNbins())
                .Data();
        binningMap[fAxes[iAxis]->GetName()] = {bins[index], bins[index + 1], bins[index + 2]};
        index += 3;
      }
      else if (fBinningTypes[iAxis] == Binning::kUser) {
        isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], fAxes[iAxis]->GetNbins(), 1, 1, min, max);
        binCoords += TString::Format(" | (U) %s %d %d %d [%d,%d] [%d,%d]", fAxes[iAxis]->GetName(),
                                     fAxes[iAxis]->GetNbins(), 1, 1, min, max, 1, fAxes[iAxis]->GetNbins())
                         .Data();
        // binningMap[fAxes[iAxis]->GetName()] = {1, 1, bins[index]};
        binningMap[fAxes[iAxis]->GetName()] = {fAxes[iAxis]->GetNbins(), 1, 1};
        index++;
      }
      else {
        NLogger::Error("[NBinning::PrintContent] Unknown binning type [%d]", fBinningTypes[iAxis]);
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

Long64_t NBinning::FillAll(std::vector<Long64_t> & ids)
{
  ///
  /// Fill content binnings from mapping
  ///
  Long64_t nBinsFilled = 0;

  // fContent->Reset();

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
    else if (fBinningTypes[idx] == Binning::kMultiple) {
      // NLogger::Debug("Binning %d: %d", i, axes[idim]->GetNbins());
      content[idx].push_back({p[0], p[1], p[2], p[3]});
    }
    else if (fBinningTypes[idx] == Binning::kUser) {
      // TODO: Fix nbins for user binning (it should ok now)
      content[idx].push_back({p[0], 1});
    }
    else {
      NLogger::Error("[NBinning::FillAll] Unknown binning type %d", fBinningTypes[idx]);
      continue;
    }
    maxs[idx] = maxs[idx] + 1;
  }
  delete[] p;

  // loop over content vector and set axis types
  for (size_t i = 0; i < content.size(); i++) {
    if (content[i].size() == 0) {
      NLogger::Warning("No content for binning %zu", i);
      continue;
    }

    if (content[i].size() > 1) fAxisTypes[i] = AxisType::kVariable;
  }

  // Loop over all binning combinations
  NDimensionalExecutor executor(mins, maxs);
  auto                 binning_task = [&content, &nBinsFilled, &ids, this](const std::vector<int> & coords) {
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
    ids.push_back(pointContentBin);
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
    NLogger::Trace("Adding binning %d: %d %d %d %d", i, point[0], point[1], point[2], point[3]);
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
    NLogger::Trace("Adding partial binning %d: %d %d %d %d", i - 1, id, rebin, rebin_start, bin);
    AddBinning(id, {rebin, rebin_start, bin}, 1);
  }

  return true;
}
bool NBinning::AddBinningViaBinWidths(int id, std::vector<std::vector<int>> widths)
{
  ///
  /// Add binning via bin widths
  ///
  // NLogger::Trace("Adding binning via bin widths for id=%d", id);
  std::vector<int> mins;
  // Print vector of widths
  mins.push_back(1);
  for (auto & w : widths) {
    // NLogger::Trace("Adding binning via bin widths: %s", NUtils::GetCoordsString(w, -1).c_str());
    TAxis * axis    = fAxes[id - 1];
    int     width   = w[0];
    int     nWidths = w.size() > 1 ? w[1] : axis->GetNbins() / width;
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
    else if (fBinningTypes[iAxis] == Binning::kUser) {
      // isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], 1, 1, c[index], min, max);
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], fAxes[iAxis]->GetNbins(), 1, 1, min, max);
      index++;
    }
    else {
      NLogger::Error("[NBinning::GetCoordsRange] Unknown binning type %d", fBinningTypes[iAxis]);
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
    // return Binning::kSingle;
    return Binning::kUndefined;
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
    else if (fBinningTypes[iAxis] == Binning::kUser) {
      isValid = NUtils::GetAxisRangeInBase(fAxes[iAxis], fAxes[iAxis]->GetNbins(), 1, 1, min, max);
      index++;
    }
    else {
      NLogger::Error("[NBinning::GetAxisRanges] Unknown binning type [%d]", fBinningTypes[iAxis]);
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
std::vector<int> NBinning::GetAxisBinning(int axisId, const std::vector<int> & c) const
{
  ///
  /// Returns axis binning for given axis id
  ///
  std::vector<int> binning;

  int iAxis = 0;
  int index = 0;
  for (int iAxis = 0; iAxis < fAxes.size(); iAxis++) {
    // for (int i = 0; i < fContent->GetNdimensions(); i += 3) {
    int min;
    int max;
    if (fBinningTypes[iAxis] == Binning::kSingle) {
      if (iAxis == axisId) {
        binning.push_back(1);
        binning.push_back(1);
        binning.push_back(c[index]);
        break;
      }
      index++;
    }
    else if (fBinningTypes[iAxis] == Binning::kMultiple) {
      if (iAxis == axisId) {
        binning.push_back(c[index]);
        binning.push_back(c[index + 1]);
        binning.push_back(c[index + 2]);
        break;
      }
      index += 3;
    }
    else if (fBinningTypes[iAxis] == Binning::kUser) {
      if (iAxis == axisId) {
        binning.push_back(c[index]);
        binning.push_back(1);
        binning.push_back(1);
        break;
      }
      index++;
    }
    else {
      NLogger::Error("[NBinning::GetAxisBinning] Unknown binning type [%d]", fBinningTypes[iAxis]);
      continue;
    }
  }

  return binning;
}

bool NBinning::GetAxisRange(int axisId, double & min, double & max, std::vector<int> c) const
{
  ///
  /// Get axis range for given axis id
  ///
  int    minBin  = 0;
  int    maxBin  = 0;
  Bool_t isValid = GetAxisRangeInBase(axisId, minBin, maxBin, c);

  min = fAxes[axisId]->GetBinLowEdge(minBin);
  max = fAxes[axisId]->GetBinUpEdge(maxBin);
  return isValid;
}

bool NBinning::GetAxisRangeInBase(int axisId, int & min, int & max, std::vector<int> c) const
{
  ///
  /// Get axis range for given axis id
  ///
  if (axisId < 0 || axisId >= fAxes.size()) {
    NLogger::Error("Invalid axis id %d", axisId);
    return false;
  }

  // Generate c if not provided

  int    minBin  = 0;
  int    maxBin  = 0;
  Bool_t isValid = false;
  if (fBinningTypes[axisId] == Binning::kSingle) {
    isValid = NUtils::GetAxisRangeInBase(fAxes[axisId], 1, 1, c[0], minBin, maxBin);
  }
  else if (fBinningTypes[axisId] == Binning::kMultiple) {
    isValid = NUtils::GetAxisRangeInBase(fAxes[axisId], c[0], c[1], c[2], minBin, maxBin);
  }

  min = minBin;
  max = maxBin;
  return isValid;
}

TObjArray * NBinning::GenerateListOfAxes()
{
  ///
  /// Create and return TObjArray of axes
  ///

  // TODO: Verify memery leak and its posible consequence for user

  TObjArray * axesArray = new TObjArray();

  // loop over all axes and create TObjArray
  for (int i = 0; i < fAxes.size(); i++) {
    TAxis * axis    = (TAxis *)fAxes[i];
    TAxis * axisNew = (TAxis *)axis->Clone();

    std::string name = axis->GetName();
    NLogger::Trace("NBinning::GetListOfAxes(): Binning '%s': %d", name.c_str(), axis->GetNbins());

    double bins[axis->GetNbins() + 1];
    int    count  = 0;
    int    iBin   = 1; // start from bin 1
    bins[count++] = axis->GetBinLowEdge(1);

    NBinningDef *                                        def        = GetDefinition();
    std::map<std::string, std::vector<std::vector<int>>> definition = def->GetDefinition();
    for (auto & v : definition.at(name)) {
      NLogger::Trace("  %s", NUtils::GetCoordsString(v, -1).c_str());

      int n = v.size() > 1 ? v[1] : axis->GetNbins() / v[0];
      for (int i = 0; i < n; i++) {
        iBin += v[0];
        bins[count++] = axis->GetBinLowEdge(iBin);
      }
    }
    // loop over bins and print
    for (int i = 0; i < count; i++) {
      NLogger::Trace("  %s: %d %f", axis->GetName(), i + 1, bins[i]);
    }
    axisNew->Set(count - 1, bins);
    axesArray->Add(axisNew);
  }

  return axesArray;
}

bool NBinning::SetAxisType(int axisId, AxisType at)
{
  ///
  /// Set axis type
  ///
  if (axisId < 0 || axisId >= fAxes.size()) {
    NLogger::Error("Invalid axis id %d", axisId);
    return false;
  }
  fAxisTypes[axisId] = at;
  return true;
}

AxisType NBinning::GetAxisType(int i) const
{
  ///
  /// Get axis type
  ///
  if (i < 0 || i >= fAxisTypes.size()) {
    NLogger::Error("Invalid axis type %d", i);
    return AxisType::kUndefined; // Undefined
  }
  return fAxisTypes[i];
}
char NBinning::GetAxisTypeChar(int i) const
{
  if (i < 0 || i >= fAxisTypes.size()) {
    NLogger::Error("Invalid axis type %d", i);
    return 'X'; // Undefined
  }
  switch (fAxisTypes[i]) {
  case AxisType::kFixed: return 'F';
  case AxisType::kVariable: return 'V';
  case AxisType::kUndefined:
  default: return 'X';
  }
}

std::vector<int> NBinning::GetAxisIndexes(AxisType type) const
{
  ///
  /// Get axis indexes by axes type
  ///
  std::vector<int> ids;
  for (int i = 0; i < fAxes.size(); i++) {
    if (fAxisTypes[i] == type) {
      NLogger::Trace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
      ids.push_back(i);
    }
    else {
      NLogger::Trace("Axis %d: %s type=%d [not selected]", i, fAxes[i]->GetName(), fAxisTypes[i]);
    }
  }
  return ids;
}

std::vector<TAxis *> NBinning::GetAxesByType(AxisType type) const
{
  ///
  /// Get axes by type
  ///
  std::vector<TAxis *> axes;
  for (int i = 0; i < fAxes.size(); i++) {
    if (fAxisTypes[i] == type) {
      NLogger::Trace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
      axes.push_back(fAxes[i]);
    }
    else {
      NLogger::Trace("Axis %d: %s type=%d [not selected]", i, fAxes[i]->GetName(), fAxisTypes[i]);
    }
  }
  return axes;
}

std::vector<int> NBinning::GetAxisIndexesByNames(std::vector<std::string> names) const
{
  ///
  /// Get axis indexes by names
  ///
  std::vector<int> ids;
  for (auto & name : names) {
    for (int i = 0; i < fAxes.size(); i++) {
      if (fAxes[i]->GetName() == name) {
        NLogger::Trace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
        ids.push_back(i);
        break; // break inner loop if found
      }
    }
  }
  return ids;
}

std::vector<std::string> NBinning::GetAxisNamesByType(AxisType type) const
{
  ///
  /// Get axis names by type
  ///
  std::vector<std::string> names;
  for (int i = 0; i < fAxes.size(); i++) {
    if (fAxisTypes[i] == type) {
      NLogger::Trace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
      names.push_back(fAxes[i]->GetName());
      continue;
    }
    else {
      NLogger::Trace("Axis %d: %s type=%d [not selected]", i, fAxes[i]->GetName(), fAxisTypes[i]);
    }
  }
  return names;
}
std::vector<std::string> NBinning::GetAxisNamesByIndexes(std::vector<int> ids) const
{
  ///
  /// Get axis names by indexes
  ///
  std::vector<std::string> names;
  for (auto & id : ids) {
    if (id < 0 || id >= fAxes.size()) {
      NLogger::Error("Invalid axis id %d", id);
      continue;
    }
    NLogger::Trace("Axis %d: %s type=%d", id, fAxes[id]->GetName(), fAxisTypes[id]);
    names.push_back(fAxes[id]->GetName());
  }
  return names;
}

NBinningDef * NBinning::GetDefinition(const std::string & name)
{

  ///
  /// Get binning definition by name
  ///

  if (name.empty()) {
    if (fCurrentDefinitionName.empty()) {
      return nullptr;
    }
    // check if current definition exists
    if (fDefinitions.find(fCurrentDefinitionName) == fDefinitions.end()) {
      NLogger::Error("NBinning::GetDefinition: Current definition '%s' not found", fCurrentDefinitionName.c_str());
      return nullptr;
    }
    NLogger::Trace("NBinning::GetDefinition: Using current definition '%s'", fCurrentDefinitionName.c_str());
    return fDefinitions.at(fCurrentDefinitionName);
  }

  if (fDefinitions.find(name) == fDefinitions.end()) {
    NLogger::Error("NBinning::GetDefinition: Definition '%s' not found", name.c_str());
    return nullptr;
  }
  NLogger::Debug("NBinning::GetDefinition: Using definition '%s'", name.c_str());
  fCurrentDefinitionName = name;
  return fDefinitions.at(name);
}

void NBinning::AddBinningDefinition(std::string name, std::map<std::string, std::vector<std::vector<int>>> binning,
                                    bool forceDefault)
{
  ///
  /// Add binning definition
  ///
  if (fDefinitions.find(name) != fDefinitions.end()) {
    NLogger::Error("Binning definition '%s' already exists", name.c_str());
    return;
  }
  NBinningDef * def  = new NBinningDef(binning, this);
  fDefinitions[name] = def;
  fMap->Reset();
  // loop over binning axes
  for (size_t i = 0; i < fAxes.size(); i++) {
    TAxis *     axis     = fAxes[i];
    std::string axisName = axis->GetName();
    // Ndmspc::NLogger::Debug("Axis %zu: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, axisName.c_str(),
    //                        axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    if (!binning[axisName].empty()) {
      AddBinningViaBinWidths(i + 1, binning[axisName]);
      // GetDefinition()[axisName] = binning[axisName];
      // def->SetAxisDefinition(axisName, binning[axisName]);
    }
    else {
      AddBinning(i + 1, {axis->GetNbins(), 1, 1}, 1);
      // GetDefinition()[axisName] = {{axis->GetNbins()}};
      // def->SetAxisDefinition(axisName, {{axis->GetNbins()}});
    }
  }

  Long64_t nStart = fContent->GetNbins();
  // def->SetStartId(nStart);
  // std::vector<Long64_t> entries;
  Long64_t nFilled = FillAll(def->GetIds());
  // def->Print();
  // NLogger::Debug("NBinning::AddBinningDefinition: Filled %lld bins for definition '%s'", nFilled,
  //                NUtils::GetCoordsString(def->GetIds(), -1).c_str());
  // def->SetNEntries(nFilled);

  if (forceDefault || fCurrentDefinitionName.empty()) {
    fCurrentDefinitionName = name;
    NLogger::Trace("Set default binning definition '%s'", fCurrentDefinitionName.c_str());
  }
  else {
    NLogger::Trace("Added binning definition '%s' [not default]", name.c_str());
  }
}

NBinningPoint * NBinning::GetPoint(int id, std::string binning)
{
  ///
  /// Get binning point by id and binning name
  ///

  NBinningDef * def = GetDefinition(binning);
  if (def == nullptr) {
    NLogger::Error("NBinning::FillPoint: Definition '%s' not found", binning.c_str());
    return nullptr;
  }

  Long64_t linBin = def->GetId(id);
  fContent->GetBinContent(linBin, fPoint->GetCoords());
  fPoint->RecalculateStorageCoords();

  return fPoint;
}

bool NBinning::SetCfg(const json & cfg)
{
  ///
  /// Set configuration
  ///
  if (fPoint == nullptr) {
    NLogger::Error("NBinning::SetCfg: Point not initialized");
    return false;
  }
  fPoint->SetCfg(cfg);

  return true;
}

} // namespace Ndmspc
