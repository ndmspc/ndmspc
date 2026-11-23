#include <string>
#include <vector>
#include "TAxis.h"
#include <TAttAxis.h>
#include <TObjArray.h>
#include "NBinningDef.h"
#include "NBinningPoint.h"
#include "NDimensionalExecutor.h"
#include "NUtils.h"
#include "NLogger.h"

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
    NLogError("NBinning(TObjArray * axes) : axes is nullptr");
    return;
  }

  for (int i = 0; i < axes->GetEntriesFast(); i++) {
    TAxis * axis = dynamic_cast<TAxis *>(axes->At(i));
    if (axis) {
      fAxes.push_back((TAxis *)axis->Clone());
    }
    else {
      NLogError("NBinning: Axis %d is not a TAxis", i);
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
void NBinning::Reset()
{
  ///
  /// Reset binning
  ///
  fMap->Reset();
  fContent->Reset();
  if (fPoint != nullptr) {
    fPoint->Reset();
  }
}

void NBinning::Initialize()
{
  ///
  /// Initialize binning
  ///

  if (fAxes.size() == 0) {
    NLogError("No axes provided");
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
    // NLogDebug("Axis %d: %s nbins=%d", i, fAxes[i]->GetName(), nbins);
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
      // TODO: Optimized beiing for alphanumeric axis (maybe no need to fix)
      // binningType = Binning::kSingle;
      // nContentDims++;
      binningType = Binning::kMultiple;
      nContentDims += 3;
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
  fMap = new THnSparseI("ngntBinningMap", "NGnTree binning map", dimBinning, nbinsBinning, xminBinning, xmaxBinning);
  if (fMap == nullptr) {
    NLogError("Cannot create binnings !!!");
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
      // NLogDebug("Binning %d: %d", i, axes[idim]->GetNbins());
      nbinsBinningContent[iContentDim] = fAxes[i]->GetNbins();
      xminBinningContent[iContentDim]  = 0;
      xmaxBinningContent[iContentDim]  = fAxes[i]->GetNbins();
      iContentDim++;
      NLogTrace("[S] Binning %d: %d", i, fAxes[i]->GetNbins());
    }
    else if (fBinningTypes[i] == Binning::kMultiple) {
      for (int j = 0; j < 3; j++) {
        nbinsBinningContent[iContentDim + j] = fAxes[i]->GetNbins();
        xminBinningContent[iContentDim + j]  = 0;
        xmaxBinningContent[iContentDim + j]  = fAxes[i]->GetNbins();
      }
      iContentDim += 3;
      NLogTrace("[M] Binning %d: %d", i, fAxes[i]->GetNbins());
    }
    else if (fBinningTypes[i] == Binning::kUser) {
      // NLogDebug("Binning %d: %d", i, axes[idim]->GetNbins());
      nbinsBinningContent[iContentDim] = fAxes[i]->GetNbins();
      xminBinningContent[iContentDim]  = 0;
      xmaxBinningContent[iContentDim]  = fAxes[i]->GetNbins();
      iContentDim++;
      NLogTrace("[U] Binning %d: %d", i, fAxes[i]->GetNbins());
    }
    else {
      NLogError("Invalid binning type %d", fBinningTypes[i]);
      return;
    }
  }
  fContent = new THnSparseI("ngntBinningContent", "NGnTree binning content", dimBinningContent, nbinsBinningContent,
                            xminBinningContent, xmaxBinningContent);

  int                      iContentAxis = 0;
  std::vector<std::string> types        = {"rebin", "start", "bin"};
  for (int i = 0; i < dim; i++) {
    std::string name  = fAxes[i]->GetName();
    std::string title = fAxes[i]->GetName();
    // NLogTrace("Binning %d: %d %d", i, axes[i]->GetNbins(), fBinningTypes[i]);
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
      // NLogDebug("Binning %d: %d", i, axes[idim]->GetNbins());
    }
    else if (fBinningTypes[i] == Binning::kUser) {
      fContent->GetAxis(iContentAxis)->SetNameTitle(name.c_str(), title.c_str());
      iContentAxis++;
    }
  }
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    NLogTrace("Axis[fContent] %d: %s nbins=%d", i, fContent->GetAxis(i)->GetName(), fContent->GetAxis(i)->GetNbins());
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
    NLogError("THnSparse fMap is null");
    return;
  }

  TString opt(option);
  opt.ToLower();

  NLogInfo("NBinning base axes:");
  for (size_t i = 0; i < fAxes.size(); i++) {
    NLogInfo("  Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, fAxes[i]->GetName(), fAxes[i]->GetTitle(),
             fAxes[i]->GetNbins(), fAxes[i]->GetXmin(), fAxes[i]->GetXmax());
  }

  if (opt.Contains("A")) {
    NLogInfo("NBinning map name='%s' title='%s'", fMap->GetName(), fMap->GetTitle());
    NLogInfo("    dimensions: %d", fMap->GetNdimensions());
    // loop over all axes and print name title
    for (int i = 0; i < fMap->GetNdimensions(); i++) {
      NLogInfo("    [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, fMap->GetAxis(i)->GetName(),
               fMap->GetAxis(i)->GetTitle(), fMap->GetAxis(i)->GetNbins(), fMap->GetAxis(i)->GetXmin(),
               fMap->GetAxis(i)->GetXmax());
    }
    NLogInfo("    filled bins = %lld", fMap->GetNbins());
    NLogInfo("NBinning content name='%s' title='%s'", fContent->GetName(), fContent->GetTitle());
    NLogInfo("    dimensions: %d", fContent->GetNdimensions());
    // loop over all axes and print name title
    for (int i = 0; i < fContent->GetNdimensions(); i++) {
      NLogInfo("    [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, fContent->GetAxis(i)->GetName(),
               fContent->GetAxis(i)->GetTitle(), fContent->GetAxis(i)->GetNbins(), fContent->GetAxis(i)->GetXmin(),
               fContent->GetAxis(i)->GetXmax());
    }
  }
  NLogInfo("NBinning content:");
  NLogInfo("    filled bins = %lld", fContent->GetNbins());
  // print list of definitions names from fDefinitions
  NLogInfo("NBinning definitions:");
  for (const auto & kv : fDefinitions) {
    if (kv.first == fCurrentDefinitionName) {
      NLogInfo("  '%s' (current)", kv.first.c_str());
    }
    else {
      NLogInfo("  '%s'", kv.first.c_str());
    }
  }

  if (option && opt.Contains("all")) {
    NLogInfo("NBinning content:");
    PrintContent(option);
  }

  // // loop over definition and print
  // for (const auto & kv : fDefinition) {
  //   NLogInfo("Binning '%s':", kv.first.c_str());
  //   for (const auto & v : kv.second) {
  //     NLogInfo("  %s", NUtils::GetCoordsString(v, -1).c_str());
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
  // NLogInfo("Using %zu threads ...", num_threads_to_use);
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
    Long64_t    idx       = cSparse->GetBin(bins, false);
    std::string binCoords = NUtils::GetCoordsString(NUtils::ArrayToVector(bins, cSparse->GetNdimensions()), -1);
    NLogTrace("Bin %lld: %f %s idx=%lld content=%f", linBin, v, binCoords.c_str(), idx, v);

    size_t iAxis   = 0;
    bool   isValid = false;
    // for (int i = 0; i < cSparse->GetNdimensions(); i += 3) {
    int index = 0;
    for (iAxis = 0; iAxis < fAxes.size(); iAxis++) {
      int min;
      int max;
      // Print type of binning
      // NLogTrace("Axis %d: %s [%s]", iAxis, fAxes[iAxis]->GetName(),
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
        NLogError("[NBinning::PrintContent] Unknown binning type [%d]", fBinningTypes[iAxis]);
        continue;
      }
      if (!isValid) {
        NLogError("Cannot get axis range for axis %d", iAxis);
        continue;
      }
    }
    if (!isValid) {
      // NLogError("Cannot get axis range for axis %d", iAxis);
      NLogError("Bin %lld: %s  [not valid for axis=%s]", linBin, binCoords.c_str(), fAxes[iAxis]->GetName());
      continue;
    }
    NLogDebug("Bin %lld: %s", linBin, binCoords.c_str());
  }
  delete[] bins;
}

NBinningPointAxis NBinningPointAxis::Label(const std::string & value)
{
  NBinningPointAxis one;
  one.form  = Form::kLabel;
  one.label = value;
  return one;
}

NBinningPointAxis NBinningPointAxis::Bin(int bin)
{
  NBinningPointAxis one;
  one.form   = Form::kBin;
  one.minBin = bin;
  one.maxBin = bin;
  return one;
}

NBinningPointAxis NBinningPointAxis::Bins(int first, int last)
{
  NBinningPointAxis one;
  one.form   = Form::kRange;
  one.minBin = first;
  one.maxBin = last;
  if (first == OPEN && last == OPEN) one.form = Form::kAll;
  return one;
}

NBinningPointAxis NBinningPointAxis::Range(double first, double last)
{
  NBinningPointAxis one;
  one.form = Form::kValueRange;
  one.from = first;
  one.to   = last;
  return one;
}

NBinningPointAxis NBinningPointAxis::Rebin(int width, int start, int id)
{
  NBinningPointAxis one;
  one.form  = Form::kRebin;
  one.rebin = width;
  one.start = start;
  one.id    = id;
  return one;
}

NBinningPointAxis PointAxisFromJson(const json & value, bool * ok)
{
  ///
  /// Read one axis' selection from its JSON form
  ///

  auto done = [ok](const NBinningPointAxis & one) {
    if (ok != nullptr) *ok = true;
    return one;
  };
  auto fail = [ok]() {
    if (ok != nullptr) *ok = false;
    return NBinningPointAxis::All();
  };

  if (value.is_string()) {
    const std::string text = value.get<std::string>();
    if (text == "*" || text == "all") return done(NBinningPointAxis::All());
    return done(NBinningPointAxis::Label(text));
  }

  if (value.is_number_integer()) return done(NBinningPointAxis::Bin(value.get<int>()));

  if (value.is_object()) {
    if (value.contains("label") && value["label"].is_string()) {
      return done(NBinningPointAxis::Label(value["label"].get<std::string>()));
    }
    if (value.contains("rebin")) {
      return done(NBinningPointAxis::Rebin(value.value("rebin", 1), value.value("start", 1), value.value("id", 1)));
    }
    if (value.contains("from") || value.contains("to")) {
      const double from = value.value("from", 0.0);
      const double to   = value.value("to", 0.0);
      return done(NBinningPointAxis::Range(from, to));
    }
    return fail();
  }

  if (value.is_array()) {
    // An empty array is the whole axis; otherwise `[min]`, `[min, max]` with null = open.
    if (value.empty()) return done(NBinningPointAxis::All());

    const int minBin = value[0].is_null() ? NBinningPointAxis::OPEN : value[0].get<int>();
    const int maxBin = value.size() < 2 || value[1].is_null() ? NBinningPointAxis::OPEN
                                                             : value[1].get<int>();
    if (value.size() == 1) return done(NBinningPointAxis::Bins(minBin, NBinningPointAxis::OPEN));
    return done(NBinningPointAxis::Bins(minBin, maxBin));
  }

  return fail();
}

std::vector<std::vector<NBinningPointAxis>> PointsFromJson(const json & value)
{
  ///
  /// Read a point list from its JSON form
  ///

  std::vector<std::vector<NBinningPointAxis>> points;
  if (!value.is_array()) return points;

  for (const auto & entry : value) {
    if (!entry.is_array()) continue;
    std::vector<NBinningPointAxis> point;
    bool                           readable = true;
    for (const auto & axis : entry) {
      bool               ok  = false;
      NBinningPointAxis  one = PointAxisFromJson(axis, &ok);
      if (!ok) readable = false;
      point.push_back(std::move(one));
    }
    if (readable) points.push_back(std::move(point));
  }
  return points;
}

std::vector<std::vector<std::vector<int>>> NBinning::CollectAxisContent(NBinningDef * def)
{
  ///
  /// The cheap half of a fill: one walk of the mapping, one entry per mapped bin.
  ///

  std::vector<std::vector<std::vector<int>>> content(fMap->GetAxis(0)->GetNbins());

  THnSparse *                                     cSparse = fMap;
  Int_t *                                         p       = new Int_t[cSparse->GetNdimensions()];
  Long64_t                                        linBin  = 0;
  std::unique_ptr<ROOT::Internal::THnBaseBinIter> iter{cSparse->CreateIter(true /*use axis range*/)};
  while ((linBin = iter->Next()) >= 0) {
    Double_t v = cSparse->GetBinContent(linBin, p);
    int      idx = p[0] - 1;
    if (idx < 0 || idx >= static_cast<int>(content.size())) {
      NLogError("NBinning::CollectAxisContent: Bin %lld names axis %d outside [1,%zu]", linBin, p[0], content.size());
      continue;
    }
    NLogTrace("NBinning::CollectAxisContent: Bin %lld: %d %d %d %d type=%d content=%f", linBin, p[0], p[1], p[2], p[3],
              fBinningTypes[idx], v);
    if (fBinningTypes[idx] == Binning::kSingle) {
      content[idx].push_back({p[0], p[3]});
    }
    else if (fBinningTypes[idx] == Binning::kMultiple) {
      content[idx].push_back({p[0], p[1], p[2], p[3]});
    }
    else if (fBinningTypes[idx] == Binning::kUser) {
      content[idx].push_back({p[0], 1});
    }
    else {
      NLogError("NBinning::CollectAxisContent: Unknown binning type %d", fBinningTypes[idx]);
      continue;
    }
  }
  delete[] p;

  // An axis with no mapped entry is not a variable axis of the definition.
  for (size_t i = 0; i < content.size(); i++) {
    if (content[i].size() == 0) {
      NLogWarning("NBinning::CollectAxisContent: No content for binning %zu", i);
      continue;
    }
    fAxisTypes[i] = AxisType::kVariable;
    NLogTrace("NBinning::CollectAxisContent: Axis id=%zu name=%s bins=%zu", i, fAxes[i]->GetName(), content[i].size());
    if (def == nullptr) continue;
    bool found = false;
    for (size_t j = 0; j < def->GetVariableAxes().size(); j++) {
      if (def->GetVariableAxes()[j] == static_cast<int>(i)) {
        found = true;
        break;
      }
    }
    if (!found) def->AddVariableAxis(i);
  }

  return content;
}

/** The base-bin range an entry of an axis' content covers: `{min, max}`. */
static std::pair<int, int> EntryBaseRange(TAxis * axis, const std::vector<int> & entry)
{
  // A multiple axis stores {dim, rebin, start, bin}; a single/user axis {dim, bin}.
  if (entry.size() < 4) return {entry.back(), entry.back()};

  int min = 0;
  int max = 0;
  if (!NUtils::GetAxisRangeInBase(axis, entry[1], entry[2], entry[3], min, max)) return {0, -1};
  return {min, max};
}

std::vector<std::vector<int>> NBinning::ResolvePoint(const std::vector<NBinningPointAxis> & point,
                                                     const std::vector<std::vector<std::vector<int>>> * content,
                                                     const std::string & definitionName)
{
  ///
  /// Resolve one point's per-axis selections to the indices of the mapped entries they cover.
  ///

  std::vector<std::vector<std::vector<int>>> local;
  if (content == nullptr) {
    // The map is cleared once a fill completes, so a standalone call rebuilds it from the
    // definition it is reading points against.
    const std::string name = definitionName.empty() ? fCurrentDefinitionName : definitionName;
    const auto        it   = fDefinitions.find(name);
    if (it == fDefinitions.end()) {
      NLogError("NBinning::ResolvePoint: Binning definition '%s' does not exist", name.c_str());
      return {};
    }
    fMap->Reset();
    if (!ApplyDefinitionBinning(name, it->second->GetDefinition())) return {};
    local   = CollectAxisContent(it->second);
    content = &local;
  }
  const auto & entries = *content;

  std::vector<std::vector<int>> resolved;
  resolved.reserve(entries.size());

  for (size_t axis = 0; axis < entries.size(); axis++) {
    // An axis the point does not reach is taken whole, as All() would.
    const NBinningPointAxis selection = axis < point.size() ? point[axis] : NBinningPointAxis::All();

    int  minBin = NBinningPointAxis::OPEN;
    int  maxBin = NBinningPointAxis::OPEN;
    bool matched = true;
    switch (selection.form) {
    case NBinningPointAxis::Form::kAll:
      break;
    case NBinningPointAxis::Form::kLabel: {
      TAxis * a = fAxes[axis];
      if (!a->IsAlphanumeric()) {
        NLogWarning("NBinning::ResolvePoint: Axis '%s' (id=%zu) is not categorical, label '%s' ignored",
                    a->GetName(), axis, selection.label.c_str());
        matched = false;
        break;
      }
      int bin = a->FindFixBin(selection.label.c_str());
      if (bin < 1 || bin > a->GetNbins()) {
        NLogWarning("NBinning::ResolvePoint: Axis '%s' has no bin labelled '%s'", a->GetName(),
                    selection.label.c_str());
        matched = false;
        break;
      }
      minBin = maxBin = bin;
      break;
    }
    case NBinningPointAxis::Form::kBin:
    case NBinningPointAxis::Form::kRange:
      minBin = selection.minBin;
      maxBin = selection.maxBin;
      break;
    case NBinningPointAxis::Form::kValueRange: {
      TAxis * a = fAxes[axis];
      int     first = a->FindFixBin(selection.from);
      int     last  = a->FindFixBin(selection.to);
      if (first < 1) first = 1;
      if (last > a->GetNbins()) last = a->GetNbins();
      minBin = first;
      maxBin = last;
      break;
    }
    case NBinningPointAxis::Form::kRebin: {
      int min = 0;
      int max = 0;
      if (!NUtils::GetAxisRangeInBase(fAxes[axis], selection.rebin, selection.start, selection.id, min, max)) {
        NLogWarning("NBinning::ResolvePoint: Axis '%s' has no bin group {%d,%d,%d}", fAxes[axis]->GetName(),
                    selection.rebin, selection.start, selection.id);
        matched = false;
        break;
      }
      minBin = min;
      maxBin = max;
      break;
    }
    }

    // A selection naming nothing describes no point at all: the whole point goes.
    if (!matched) return {};

    // Overlap: an entry is taken when its base range meets the selection, with an open end unbounded.
    std::vector<int> taken;
    for (size_t at = 0; at < entries[axis].size(); at++) {
      const auto [first, last] = EntryBaseRange(fAxes[axis], entries[axis][at]);
      if (last < first) continue;
      if (minBin != NBinningPointAxis::OPEN && last < minBin) continue;
      if (maxBin != NBinningPointAxis::OPEN && first > maxBin) continue;
      taken.push_back(static_cast<int>(at) + 1);
    }

    if (taken.empty()) {
      NLogWarning("NBinning::ResolvePoint: Axis '%s' (id=%zu) selection matches no bin", fAxes[axis]->GetName(), axis);
      return {};
    }
    resolved.push_back(std::move(taken));
  }

  return resolved;
}

void NBinning::FillResolvedPoint(const std::vector<std::vector<std::vector<int>>> & content,
                                 const std::vector<std::vector<int>> & resolved, std::set<Long64_t> & seen,
                                 Long64_t & nBinsFilled, Long64_t nTotalBins,
                                 const std::chrono::high_resolution_clock::time_point & start, bool runOutput,
                                 NBinningDef * def)
{
  ///
  /// Fill one already-resolved point: the product of its per-axis entry indices.
  ///
  /// The executor walks the **ordinals** of the selection, not the entry indices themselves: a
  /// selection over a rebinned axis may pick entries that are not a contiguous range, which bounds
  /// cannot express. Walking `1..size` per axis and translating through the list keeps the whole
  /// machinery `NDimensionalExecutor` brings — thread modes, IPC, cancellation — for a point that
  /// only covers part of an axis.
  ///

  if (resolved.empty()) return;

  std::vector<int> mins;
  std::vector<int> maxs;
  for (const auto & axis : resolved) {
    mins.push_back(1);
    maxs.push_back(static_cast<int>(axis.size()));
  }

  auto fill = [&content, &resolved, &seen, &nBinsFilled, nTotalBins, start, runOutput, def,
               this](const std::vector<int> & coords) {
    std::vector<int> pointContentVector;
    for (size_t i = 0; i < coords.size(); i++) {
      // The coordinate is this axis' ordinal in the selection; the list says which entry that is.
      const std::vector<int> & entry = content[i][resolved[i][coords[i] - 1] - 1];
      if (entry.size() == 2) {
        pointContentVector.push_back(entry[1]);
      }
      else {
        pointContentVector.push_back(entry[1]);
        pointContentVector.push_back(entry[2]);
        pointContentVector.push_back(entry[3]);
      }
    }

    Int_t nContentDims = fContent->GetNdimensions();
    auto  pointContent = std::make_unique<Int_t[]>(nContentDims);
    NUtils::VectorToArray(pointContentVector, pointContent.get());
    Long64_t pointContentBin = fContent->GetBin(pointContent.get());

    // A bin another point already filled is left alone; this is what overlapping points need.
    if (!seen.insert(pointContentBin).second) return;

    if (def) {
      NBinningPoint point(this);
      fContent->GetBinContent(pointContentBin, point.GetCoords());
      point.RecalculateStorageCoords();
      Long64_t linBin = def->GetContent()->GetBin(point.GetStorageCoords());
      def->GetContent()->SetBinContent(linBin, pointContentBin);
      def->GetIds().push_back(pointContentBin);
    }

    fContent->SetBinContent(pointContentBin, 1);
    nBinsFilled++;
    int refreshRate = nTotalBins / 100;
    if (refreshRate == 0) refreshRate = nTotalBins;
    if (nBinsFilled % refreshRate == 0 && nBinsFilled != nTotalBins)
      if (runOutput) Ndmspc::NUtils::ProgressBar(nBinsFilled, nTotalBins, start, "I    ");
  };

  NDimensionalExecutor executor(mins, maxs);
  executor.Execute(fill);
}

Long64_t NBinning::FillAll(NBinningDef * def)
{
  ///
  /// Fill content binnings from mapping: the full product of every axis' mapped entries.
  ///

  bool runOutput = NLogger::GetRunOutput();
  bool isWorker  = getenv("NDMSPC_WORKER_ENDPOINT") != nullptr;

  auto                                      content = CollectAxisContent(def);
  Long64_t                                  nTotalBins = 1;
  for (const auto & axis : content) {
    if (!axis.empty()) nTotalBins *= axis.size();
  }

  def->Print();
  if (runOutput && !isWorker)
    NLogRun("NBinning::FillAll: Filling '%s' total of %lld bins ...", def->GetName(), nTotalBins);
  NLogDebug("NBinning::FillAll: Filling '%s' total of %lld bins ...", def->GetName(), nTotalBins);

  auto start_par = std::chrono::high_resolution_clock::now();

  std::vector<std::vector<int>> resolved;
  for (const auto & axis : content) {
    std::vector<int> all;
    for (size_t at = 0; at < axis.size(); at++) all.push_back(static_cast<int>(at) + 1);
    resolved.push_back(std::move(all));
  }

  std::set<Long64_t> seen;
  Long64_t           nBinsFilled = 0;
  FillResolvedPoint(content, resolved, seen, nBinsFilled, nTotalBins, start_par, runOutput, def);

  if (runOutput && !isWorker) Ndmspc::NUtils::ProgressBar(nTotalBins, nTotalBins, start_par, "I    ");

  auto                                      end_par = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> par_duration = end_par - start_par;
  NLogInfo("NBinning::FillAll: Filled %lld bins in %s s", nTotalBins,
           NUtils::FormatTime(par_duration.count() / 1000).c_str());

  fMap->Reset();
  return nBinsFilled;
}

Long64_t NBinning::FillPoints(NBinningDef * def, const std::vector<std::vector<NBinningPointAxis>> & points)
{
  ///
  /// Fill only the given points instead of the whole product of bins.
  ///

  bool runOutput = NLogger::GetRunOutput();
  bool isWorker  = getenv("NDMSPC_WORKER_ENDPOINT") != nullptr;

  auto content = CollectAxisContent(def);

  // Expand each point to its own per-axis entry indices; a point that matches nothing is dropped.
  std::vector<std::vector<std::vector<int>>> expanded;
  for (const auto & point : points) {
    auto resolved = ResolvePoint(point, &content);
    if (resolved.empty()) continue;
    expanded.push_back(std::move(resolved));
  }

  Long64_t nTotalBins = 0;
  for (const auto & point : expanded) {
    Long64_t one = 1;
    for (const auto & axis : point) one *= axis.size();
    nTotalBins += one;
  }

  def->Print();
  if (runOutput && !isWorker)
    NLogRun("NBinning::FillPoints: Filling '%s' %zu point(s) totalling %lld bins ...", def->GetName(), expanded.size(),
            nTotalBins);
  NLogDebug("NBinning::FillPoints: Filling '%s' %zu point(s) totalling %lld bins ...", def->GetName(), expanded.size(),
            nTotalBins);

  auto start_par = std::chrono::high_resolution_clock::now();

  std::set<Long64_t> seen;
  Long64_t           nBinsFilled = 0;
  for (const auto & point : expanded) {
    FillResolvedPoint(content, point, seen, nBinsFilled, nTotalBins, start_par, runOutput, def);
  }

  if (runOutput && !isWorker && nTotalBins > 0)
    Ndmspc::NUtils::ProgressBar(nTotalBins, nTotalBins, start_par, "I    ");

  auto                                      end_par = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> par_duration = end_par - start_par;
  NLogInfo("NBinning::FillPoints: Filled %lld distinct bins in %s s", nBinsFilled,
           NUtils::FormatTime(par_duration.count() / 1000).c_str());

  fMap->Reset();
  return nBinsFilled;
}

bool NBinning::AddBinning(size_t id, std::vector<int> binning, size_t n)
{
  ///
  /// Add binning
  ///

  if (fAxes.size() == 0) {
    NLogError("AddBinning: No axes defined !!!");
    return false;
  }

  if (id <= 0 || id > fAxes.size()) {
    NLogError("AddBinning: Invalid binning id %d", id);
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
  for (size_t i = 0; i < n; i++) {
    NLogTrace("Adding binning %d: %d %d %d %d", i, point[0], point[1], point[2], point[3]);
    fMap->SetBinContent(point, 1);
    point[3] += 1;
  }
  delete[] point;

  return true;
}
bool NBinning::AddBinningVariable(size_t id, std::vector<int> mins)
{
  ///
  /// Add Variable binning
  ///
  if (id <= 0 || id > fAxes.size()) {
    NLogError("AddBinningVariable: Invalid binning id %d", id);
    return false;
  }

  // lopp over all mins and set binning
  for (size_t i = 1; i < mins.size(); i++) {

    if (mins[i] < 1 || mins[i] > fAxes[id - 1]->GetNbins() + 1) {
      // NLogWarning("AddBinningVariable: Invalid binning value mins=%d", mins[i]);
      return true;
    }
    // if (mins[i] == fAxes[id - 1]->GetNbins() || i == mins.size() - 1) mins[i]++;
    int rebin = mins[i] - mins[i - 1];
    if (rebin < 1) {
      NLogError("AddBinningVariable: Invalid binning value rebin=%d", rebin);
      return false;
    }

    int rebin_start = mins[i - 1] % rebin;
    int bin         = mins[i] / rebin;

    // int rebin_start = ((mins[i - 1]) % rebin) + 1;
    // int bin         = (mins[i] / rebin);
    if (rebin == 1) {
      rebin_start = 1;
      bin         = mins[i - 1];
    }
    // rebin_start + 1 is just that it looks better in the output
    // NLogDebug("Adding partial binning %d: %d %d %d %d [%d,%d]", i - 1, id, rebin, rebin_start + 1, bin,
    //                mins[i - 1], mins[i]);
    AddBinning(id, {rebin, rebin_start, bin}, 1);
  }

  return true;
}
bool NBinning::AddBinningViaBinWidths(size_t id, std::vector<std::vector<int>> widths)
{
  ///
  /// Add binning via bin widths
  ///
  // NLogTrace("Adding binning via bin widths for id=%d", id);
  std::vector<int> mins;
  // Print vector of widths
  mins.push_back(1);
  bool appliedStartSkip = false;
  for (auto & w : widths) {
    TAxis * axis    = fAxes[id - 1];
    int     width   = w[0];
    int     nWidths = w.size() > 1 ? w[1] : axis->GetNbins() / width;

    // support shorthand {width, -k} to skip k*width bins at start (apply once)
    int startGroups = 0;
    if (!appliedStartSkip && w.size() > 1 && w[1] < 0) {
      int startSkip    = (-w[1]) * width;
      mins[0]          = 1 + startSkip;
      startGroups      = startSkip / width;
      appliedStartSkip = true;
    }

    for (int iWidth = startGroups; iWidth < nWidths; iWidth++) {
      if (mins[mins.size() - 1] > axis->GetNbins()) {
        break;
      }
      mins.push_back(mins[mins.size() - 1] + width);
    }
  }

  NLogTrace("NBinning::AddBinningViaBinWidths: Adding binning via bin widths: %s",
            NUtils::GetCoordsString(mins, -1).c_str());

  return AddBinningVariable(id, mins);
}

std::vector<std::vector<int>> NBinning::GetCoordsRange(std::vector<int> c) const
{
  std::vector<std::vector<int>> coordsRange;
  std::vector<int>              ids;
  std::vector<int>              mins;
  std::vector<int>              maxs;
  // int                           iAxis = 0;
  bool isValid;
  int  index = 0;
  for (size_t iAxis = 0; iAxis < fAxes.size(); iAxis++) {
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
      NLogError("NBinning::GetCoordsRange: Unknown binning type %d", fBinningTypes[iAxis]);
      continue;
    }
    if (!isValid) {
      // NLogError("Cannot get axis range for axis %d", iAxis);
      return {};
    }
    // print min max
    NLogDebug("Axis %d: %s [%d,%d]", iAxis, fAxes[iAxis]->GetName(), min, max);
    ids.push_back(iAxis); // +1 because id starts from 1
    mins.push_back(min);
    maxs.push_back(max);
  }

  coordsRange.push_back(ids);
  coordsRange.push_back(mins);
  coordsRange.push_back(maxs);
  return coordsRange;
}

Binning NBinning::GetBinningType(size_t i) const
{
  if (i >= fBinningTypes.size()) {
    NLogError("Invalid binning type %d", i);
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

  bool isValid;
  int  index = 0;
  for (size_t iAxis = 0; iAxis < fAxes.size(); iAxis++) {
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
      NLogError("[NBinning::GetAxisRanges] Unknown binning type [%d]", fBinningTypes[iAxis]);
      continue;
    }
    if (!isValid) {
      // NLogError("Cannot get axis range for axis %d", iAxis);
      return {};
    }
    // print min max
    NLogTrace("Axis %d: %s [%d,%d]", iAxis, fAxes[iAxis]->GetName(), min, max);
    axisRanges.push_back({(int)iAxis, min, max});
  }

  return axisRanges;
}
std::vector<int> NBinning::GetAxisBinning(size_t axisId, const std::vector<int> & c) const
{
  ///
  /// Returns axis binning for given axis id
  ///
  std::vector<int> binning;

  int index = 0;
  for (size_t iAxis = 0; iAxis < fAxes.size(); iAxis++) {
    // for (int i = 0; i < fContent->GetNdimensions(); i += 3) {
    // int min;
    // int max;
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
      NLogError("[NBinning::GetAxisBinning] Unknown binning type [%d]", fBinningTypes[iAxis]);
      continue;
    }
  }

  return binning;
}

bool NBinning::GetAxisRange(size_t axisId, double & min, double & max, std::vector<int> c) const
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

bool NBinning::GetAxisRangeInBase(size_t axisId, int & min, int & max, std::vector<int> c) const
{
  ///
  /// Get axis range for given axis id
  ///
  if (axisId >= fAxes.size()) {
    NLogError("Invalid axis id %d", axisId);
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
  for (size_t i = 0; i < fAxes.size(); i++) {
    TAxis * axis    = (TAxis *)fAxes[i];
    TAxis * axisNew = (TAxis *)axis->Clone();

    std::string name = axis->GetName();
    NLogTrace("NBinning::GetListOfAxes(): Binning '%s': %d", name.c_str(), axis->GetNbins());

    auto bins = std::make_unique<double[]>(axis->GetNbins() + 1);
    // double bins[axis->GetNbins() + 1];
    int count     = 0;
    int iBin      = 1; // start from bin 1
    bins[count++] = axis->GetBinLowEdge(1);

    NBinningDef *                                        def        = GetDefinition();
    std::map<std::string, std::vector<std::vector<int>>> definition = def->GetDefinition();
    for (auto & v : definition.at(name)) {
      NLogTrace("  %s", NUtils::GetCoordsString(v, -1).c_str());

      int n = v.size() > 1 ? v[1] : axis->GetNbins() / v[0];
      for (int i = 0; i < n; i++) {
        iBin += v[0];
        bins[count++] = axis->GetBinLowEdge(iBin);
      }
    }
    // loop over bins and print
    for (int i = 0; i < count; i++) {
      NLogTrace("  %s: %d %f", axis->GetName(), i + 1, bins[i]);
    }
    axisNew->Set(count - 1, bins.get());
    axesArray->Add(axisNew);
  }

  return axesArray;
}

bool NBinning::SetAxisType(size_t axisId, AxisType at)
{
  ///
  /// Set axis type
  ///
  if (axisId >= fAxes.size()) {
    NLogError("Invalid axis id %d", axisId);
    return false;
  }
  fAxisTypes[axisId] = at;
  return true;
}

AxisType NBinning::GetAxisType(size_t i) const
{
  ///
  /// Get axis type
  ///
  if (i >= fAxisTypes.size()) {
    NLogError("Invalid axis type %d", i);
    return AxisType::kUndefined; // Undefined
  }
  return fAxisTypes[i];
}
char NBinning::GetAxisTypeChar(size_t i) const
{
  if (i >= fAxisTypes.size()) {
    NLogError("Invalid axis type %d", i);
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
  for (size_t i = 0; i < fAxes.size(); i++) {
    if (fAxisTypes[i] == type) {
      NLogTrace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
      ids.push_back(i);
    }
    else {
      NLogTrace("Axis %d: %s type=%d [not selected]", i, fAxes[i]->GetName(), fAxisTypes[i]);
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
  for (size_t i = 0; i < fAxes.size(); i++) {
    if (fAxisTypes[i] == type) {
      NLogTrace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
      axes.push_back(fAxes[i]);
    }
    else {
      NLogTrace("Axis %d: %s type=%d [not selected]", i, fAxes[i]->GetName(), fAxisTypes[i]);
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
    for (size_t i = 0; i < fAxes.size(); i++) {
      if (fAxes[i]->GetName() == name) {
        NLogTrace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
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
  for (size_t i = 0; i < fAxes.size(); i++) {
    if (fAxisTypes[i] == type) {
      NLogTrace("Axis %d: %s type=%d", i, fAxes[i]->GetName(), fAxisTypes[i]);
      names.push_back(fAxes[i]->GetName());
      continue;
    }
    else {
      NLogTrace("Axis %d: %s type=%d [not selected]", i, fAxes[i]->GetName(), fAxisTypes[i]);
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
    // FIXME: change to size_t
    if (id < 0 || id >= (int)fAxes.size()) {
      NLogError("Invalid axis id %d", id);
      continue;
    }
    NLogTrace("Axis %d: %s type=%d", id, fAxes[id]->GetName(), fAxisTypes[id]);
    names.push_back(fAxes[id]->GetName());
  }
  return names;
}

NBinningDef * NBinning::GetDefinition(const std::string & name,bool setCurrent)
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
      NLogError("NBinning::GetDefinition: Current definition '%s' not found", fCurrentDefinitionName.c_str());
      return nullptr;
    }
    NLogTrace("NBinning::GetDefinition: Using current definition '%s'", fCurrentDefinitionName.c_str());
    return fDefinitions.at(fCurrentDefinitionName);
  }

  if (fDefinitions.find(name) == fDefinitions.end()) {
    NLogError("NBinning::GetDefinition: Definition '%s' not found", name.c_str());
    return nullptr;
  }
  NLogTrace("NBinning::GetDefinition: Using definition '%s'", name.c_str());
  
  if (setCurrent) {
    fCurrentDefinitionName = name;
  }
  return fDefinitions.at(name);
}

void NBinning::AddBinningDefinition(std::string name, std::map<std::string, std::vector<std::vector<int>>> binning,
                                    const std::vector<std::vector<NBinningPointAxis>> & points, bool forceDefault)
{
  ///
  /// Add binning definition, filled with the given points only
  ///
  if (fDefinitions.find(name) != fDefinitions.end()) {
    NLogError("Binning definition '%s' already exists", name.c_str());
    return;
  }

  NLogInfo("NBinning::AddBinningDefinition: Adding binning definition '%s' (%zu point(s))", name.c_str(),
           points.size());
  NBinningDef * def = new NBinningDef(name, binning, this);
  fDefinitionNames.push_back(name);
  fDefinitions[name] = def;
  fMap->Reset();

  if (!ApplyDefinitionBinning(name, binning)) {
    return;
  }

  // Points naming axes past this definition's axes are refused: a definition's axes are fixed.
  for (const auto & point : points) {
    if (point.size() > fAxes.size()) {
      NLogError("NBinning::AddBinningDefinition: definition '%s' has %zu axes but a point pins %zu", name.c_str(),
                fAxes.size(), point.size());
      return;
    }
  }

  Long64_t nFilled = FillPoints(def, points);
  NLogTrace("NBinning::AddBinningDefinition: Filled %lld bins for definition '%s'", nFilled,
            NUtils::GetCoordsString(def->GetIds(), -1).c_str());

  if (forceDefault || fCurrentDefinitionName.empty()) {
    fCurrentDefinitionName = name;
    NLogTrace("Set default binning definition '%s'", fCurrentDefinitionName.c_str());
  }
  else {
    NLogTrace("Added binning definition '%s' [not default]", name.c_str());
  }
}

Long64_t NBinning::AppendPoints(const std::string & name, const std::vector<std::vector<NBinningPointAxis>> & points)
{
  ///
  /// Add more points to a definition that already exists
  ///

  auto it = fDefinitions.find(name);
  if (it == fDefinitions.end()) {
    NLogError("NBinning::AppendPoints: Binning definition '%s' does not exist", name.c_str());
    return 0;
  }

  NBinningDef * def = it->second;
  for (const auto & point : points) {
    if (point.size() > fAxes.size()) {
      NLogError("NBinning::AppendPoints: definition '%s' has %zu axes but a point pins %zu", name.c_str(),
                fAxes.size(), point.size());
      return 0;
    }
  }

  // The map is empty once a fill completes, so re-apply the definition's own binning to read it.
  fMap->Reset();
  if (!ApplyDefinitionBinning(name, def->GetDefinition())) {
    return 0;
  }

  auto     content = CollectAxisContent(def);
  Long64_t nTotalBins = 0;
  std::vector<std::vector<std::vector<int>>> expanded;
  for (const auto & point : points) {
    auto resolved = ResolvePoint(point, &content);
    if (resolved.empty()) continue;
    Long64_t one = 1;
    for (const auto & axis : resolved) one *= axis.size();
    nTotalBins += one;
    expanded.push_back(std::move(resolved));
  }

  // Seed the dedupe set with what the definition holds so appending is idempotent.
  std::set<Long64_t> seen(def->GetIds().begin(), def->GetIds().end());

  bool     runOutput   = NLogger::GetRunOutput();
  auto     start_par   = std::chrono::high_resolution_clock::now();
  Long64_t appended    = 0;
  for (const auto & point : expanded) {
    Long64_t before = static_cast<Long64_t>(def->GetIds().size());
    Long64_t filled = 0;
    FillResolvedPoint(content, point, seen, filled, nTotalBins, start_par, runOutput, def);
    (void)filled;
    appended += static_cast<Long64_t>(def->GetIds().size()) - before;
  }

  NLogInfo("NBinning::AppendPoints: Appended %lld new bin(s) to '%s' (%zu point(s) given)", appended, name.c_str(),
           points.size());
  fMap->Reset();
  return appended;
}

Long64_t NBinning::FillInRuns(const std::string & name, int pointsPerRun)
{
  ///
  /// Fill a definition over the whole product in several executor runs
  ///

  auto it = fDefinitions.find(name);
  if (it == fDefinitions.end()) {
    NLogError("NBinning::FillInRuns: Binning definition '%s' does not exist", name.c_str());
    return 0;
  }
  NBinningDef * def = it->second;

  // A resumed fill is the same operation: whatever is held is skipped, and only the rest is run.
  fMap->Reset();
  if (!ApplyDefinitionBinning(name, def->GetDefinition())) return 0;

  auto     content = CollectAxisContent(def);
  Long64_t nTotalBins = 1;
  for (const auto & axis : content) {
    if (!axis.empty()) nTotalBins *= axis.size();
  }

  bool runOutput = NLogger::GetRunOutput();
  bool isWorker  = getenv("NDMSPC_WORKER_ENDPOINT") != nullptr;
  if (runOutput && !isWorker)
    NLogRun("NBinning::FillInRuns: Filling '%s' total of %lld bins ...", def->GetName(), nTotalBins);
  NLogDebug("NBinning::FillInRuns: Filling '%s' total of %lld bins in runs of %d ...", def->GetName(), nTotalBins,
            pointsPerRun);

  auto start_par = std::chrono::high_resolution_clock::now();

  // Every axis whole; the chunking below narrows the outermost one per run.
  std::vector<std::vector<int>> resolved;
  for (const auto & axis : content) {
    std::vector<int> all;
    for (size_t at = 0; at < axis.size(); at++) all.push_back(static_cast<int>(at) + 1);
    resolved.push_back(std::move(all));
  }

  // Seeded from what the definition holds so a resumed run skips the bins already filled.
  std::set<Long64_t> seen(def->GetIds().begin(), def->GetIds().end());
  Long64_t           nBinsFilled = 0;
  Long64_t           nRuns       = 0;

  if (pointsPerRun <= 0 || content.empty()) {
    FillResolvedPoint(content, resolved, seen, nBinsFilled, nTotalBins, start_par, runOutput, def);
    nRuns = 1;
  }
  else {
    // Split along the outermost axis, so each run is contiguous in the order the points are read.
    const size_t outermost = resolved.front().size();
    for (size_t from = 0; from < outermost; from += static_cast<size_t>(pointsPerRun)) {
      std::vector<std::vector<int>> chunk = resolved;
      const size_t to = std::min(outermost, from + static_cast<size_t>(pointsPerRun));
      chunk.front().assign(resolved.front().begin() + from, resolved.front().begin() + to);

      NLogTrace("NBinning::FillInRuns: run %lld, outermost entries %zu..%zu of %zu", nRuns, from, to, outermost);
      FillResolvedPoint(content, chunk, seen, nBinsFilled, nTotalBins, start_par, runOutput, def);
      nRuns++;
    }
  }

  if (runOutput && !isWorker && nTotalBins > 0)
    Ndmspc::NUtils::ProgressBar(nTotalBins, nTotalBins, start_par, "I    ");

  auto                                      end_par = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> par_duration = end_par - start_par;
  NLogInfo("NBinning::FillInRuns: Filled %lld bins in %lld run(s) over %s s", nBinsFilled, nRuns,
           NUtils::FormatTime(par_duration.count() / 1000).c_str());

  fMap->Reset();
  return nBinsFilled;
}

bool NBinning::ApplyDefinitionBinning(const std::string & name,
                                      const std::map<std::string, std::vector<std::vector<int>>> & binning)
{
  ///
  /// Apply a definition's per-axis binning to the mapping
  ///

  for (size_t i = 0; i < fAxes.size(); i++) {
    TAxis *     axis     = fAxes[i];
    std::string axisName = axis->GetName();

    auto spec = binning.find(axisName);
    if (spec != binning.end() && !spec->second.empty()) {
      if (axis->IsAlphanumeric() && (spec->second.size() != 1 || spec->second[0][0] != 1)) {
        NLogError("NBinning::AddBinningDefinition: Axis '%s' is alphanumeric and is binning provided in definition "
                  "'%s'. Exiting ...",
                  axisName.c_str(), name.c_str());
        exit(1);
      }
      AddBinningViaBinWidths(i + 1, spec->second);
      SetAxisType(i, AxisType::kVariable);
    }
    else {
      if (axis->IsAlphanumeric()) {
        NLogError("NBinning::AddBinningDefinition: Axis '%s' is alphanumeric and no binning provided in definition "
                  "'%s'. Exiting ...",
                  axisName.c_str(), name.c_str());
        exit(1);
      }
      AddBinning(i + 1, {axis->GetNbins(), 1, 1}, 1);
    }
  }
  return true;
}

void NBinning::AddBinningDefinition(std::string name, std::map<std::string, std::vector<std::vector<int>>> binning,
                                    bool forceDefault)
{
  ///
  /// Add binning definition
  ///
  if (fDefinitions.find(name) != fDefinitions.end()) {
    NLogError("Binning definition '%s' already exists", name.c_str());
    return;
  }

  NLogInfo("NBinning::AddBinningDefinition: Adding binning definition '%s'", name.c_str());
  NBinningDef * def = new NBinningDef(name, binning, this);
  fDefinitionNames.push_back(name);
  fDefinitions[name]     = def;
  fCurrentDefinitionName = name;
  fMap->Reset();
  if (!ApplyDefinitionBinning(name, binning)) {
    return;
  }

  Long64_t nFilled = FillAll(def);
  NLogTrace("NBinning::AddBinningDefinition: Filled %lld bins for definition '%s'", nFilled,
            NUtils::GetCoordsString(def->GetIds(), -1).c_str());

  if (forceDefault || fCurrentDefinitionName.empty()) {
    fCurrentDefinitionName = name;
    NLogTrace("Set default binning definition '%s'", fCurrentDefinitionName.c_str());
  }
  else {
    NLogTrace("Added binning definition '%s' [not default]", name.c_str());
  }
}

NBinningPoint * NBinning::GetPoint()
{
  ///
  /// Return binning point
  ///

  if (fPoint == nullptr) {
    fPoint = new NBinningPoint(this);
  }
  return fPoint;
}

NBinningPoint * NBinning::GetPoint(size_t id, std::string binning)
{
  ///
  /// Get binning point by id and binning name
  ///

  // TODO: Optimize speed here

  NBinningDef * def = GetDefinition(binning);
  if (def == nullptr) {
    NLogError("NBinning::FillPoint: Definition '%s' not found", binning.c_str());
    return nullptr;
  }

  if (fPoint == nullptr) {
    fPoint = new NBinningPoint(this);
  }

  Long64_t linBin = def->GetId(id);
  if (linBin < 0) {
    NLogError("NBinning::GetPoint: Invalid linear bin %lld for id=%d in definition '%s'", linBin, id, binning.c_str());
    return nullptr;
  }
  if (linBin >= fContent->GetNbins()) {
    NLogError("NBinning::GetPoint: Linear bin %lld out of range [0,%lld) for id=%d in definition '%s'", linBin,
              fContent->GetNbins(), id, binning.c_str());
    return nullptr;
  }
  fContent->GetBinContent(linBin, fPoint->GetCoords());
  fPoint->RecalculateStorageCoords(linBin);

  return fPoint;
}

bool NBinning::SetCfg(const json & cfg)
{
  ///
  /// Set configuration
  ///
  GetPoint()->SetCfg(cfg);

  return true;
}

void NBinning::SetCurrentDefinitionName(const std::string & name)
{
  ///
  /// Set current binning definition name
  ///
  ///

  if (name.empty()) {
    if (!fDefinitionNames.empty()) {
      fCurrentDefinitionName = fDefinitionNames[0];
      NLogInfo("Binning definition name='' set to first available definition '%s'", fCurrentDefinitionName.c_str());
      return;
    }
    else {
      NLogError("Binning definition name='' and no definitions are available !!! Ignorred ...");
      return;
    }
  }

  if (fDefinitions.find(name) == fDefinitions.end()) {
    NLogError("Binning definition '%s' not found", name.c_str());
    return;
  }

  // check if name is already current
  if (fCurrentDefinitionName.compare(name) == 0) {
    NLogTrace("Binning definition '%s' is already current", name.c_str());
    return;
  }

  // Set current definition name
  fCurrentDefinitionName = name;

  // Reset Point
  if (fPoint == nullptr) {
    fPoint = new NBinningPoint(this);
  }
  fPoint->Reset();
}

int NBinning::GetAxisIndex(const std::string & name) const
{
  ///
  /// Get axis index by name
  ///
  for (size_t i = 0; i < fAxes.size(); i++) {
    if (name.compare(fAxes[i]->GetName()) == 0) {
      return i;
    }
  }
  // NLogError("NBinning::GetAxisIndex: Axis '%s' not found", name.c_str());
  return -1; 
}

} // namespace Ndmspc
