#include "NBinningDef.h"
#include "TObjArray.h"
#include "TObject.h"
#include "NDimensionalExecutor.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NBinning.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NBinningDef);
/// \endcond

namespace Ndmspc {
NBinningDef::NBinningDef(std::string name, std::map<std::string, std::vector<std::vector<int>>> definition,
                         NBinning * binning)
    : TObject(), fBinning(binning), fName(name), fDefinition(definition)
{
  ///
  /// Constructor
  ///

  // TODO: build vector of TAxis from the definition
  // Print definition
  // for (const auto & kv : fDefinition) {
  //   NLogDebug("NBinningDef: Binning '%s': %zu entries", kv.first.c_str(), kv.second.size());
  //   for (const auto & v : kv.second) {
  //     NLogDebug("  %s", NUtils::GetCoordsString(v, -1).c_str());
  //   }
  // }

  if (binning == nullptr) {
    // NLogError("NBinningDef: Binning is nullptr");
    return;
  }

  std::vector<TAxis *> axes = binning->GetAxes();
  for (size_t i = 0; i < axes.size(); i++) {
    TAxis *     axis     = axes[i];
    std::string axisName = axis->GetName();
    // NLogDebug("NBinningDef::NBinningDef: Axis %zu: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i,
    // axisName.c_str(),
    //                        axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    if (fDefinition[axisName].empty()) {
      SetAxisDefinition(axisName, {{axis->GetNbins()}});
    }
  }

  int        dims  = axes.size();
  Int_t *    nbins = new Int_t[dims];
  Double_t * xmin  = new Double_t[dims];
  Double_t * xmax  = new Double_t[dims];

  // fContent = new THnSparseD("content", "content", axes.size());
  TObjArray * cAxes = new TObjArray();

  // loop over all axes and create TObjArray
  for (int i = 0; i < dims; i++) {
    TAxis * axis    = (TAxis *)axes[i];
    TAxis * axisNew = (TAxis *)axis->Clone();

    std::string name = axis->GetName();
    NLogTrace("NBinningDef: Binning '%s': %d", name.c_str(), axis->GetNbins());

    auto bins = std::make_unique<double[]>(axis->GetNbins() + 1);
    // double bins[axis->GetNbins() + 1];
    int count     = 0;
    bins[count++] = axis->GetBinLowEdge(1);

    int                                                  iBin       = 0;
    std::map<std::string, std::vector<std::vector<int>>> definition = GetDefinition();
    for (auto & v : definition.at(name)) {
      // NLogDebug("  %s", NUtils::GetCoordsString(v, -1).c_str());

      int n = v.size() > 1 ? v[1] : axis->GetNbins() / v[0];
      for (int i = 0; i < n; i++) {
        iBin += v[0];
        if (iBin > axis->GetNbins()) {
          break;
        }
        bins[count++] = axis->GetBinUpEdge(iBin);
      }
    }
    // loop over bins and print
    // for (int i = 0; i < count; i++) {
    //   NLogDebug("  %s: %d %f", axis->GetName(), i + 1, bins[i]);
    // }
    axisNew->Set(count - 1, bins.get());
    cAxes->Add(axisNew);
  }

  for (int i = 0; i < cAxes->GetEntries(); i++) {
    TAxis * axis = (TAxis *)cAxes->At(i);
    nbins[i]     = axis->GetNbins();
    xmin[i]      = 0;
    xmax[i]      = axis->GetNbins();
  }
  fContent = new THnSparseL("content", "content", dims, nbins, xmin, xmax);
  // NLogInfo("---------- NBinningDef::NBinningDef: Created content THnSparse with %d dimensions", dims);

  for (int i = 0; i < cAxes->GetEntries(); i++) {
    TAxis * axis        = (TAxis *)cAxes->At(i);
    TAxis * axisContent = fContent->GetAxis(i);
    axisContent->SetName(axis->GetName());
    axisContent->SetTitle(axis->GetTitle());
    if (axis->IsVariableBinSize()) {
      axisContent->Set(fContent->GetAxis(i)->GetNbins(), axis->GetXbins()->GetArray());
    }
    else {
      axisContent->Set(axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    }

    if (axes[i]->IsAlphanumeric()) {
      for (int b = 1; b <= axis->GetNbins(); b++) {
        NLogTrace("NBinningDef::NBinningDef: Setting bin label for axis '%s' bin %d: '%s'", axis->GetName(), b,
                  axis->GetBinLabel(b));
        axisContent->SetBinLabel(b, axes[i]->GetBinLabel(b));
      }
    }
  }

  for (size_t i = 0; i < fBinning->GetAxes().size(); i++) {
    TAxis * a = fBinning->GetAxes()[i];
    if (a == nullptr) {
      NLogError("NBinningPoint::GetTitle: Axis %d is nullptr !!!", i);
      continue;
    }
  }
  // cleanup cAxes
  for (int i = 0; i < cAxes->GetEntries(); i++) {
    TAxis * axis = (TAxis *)cAxes->At(i);
    delete axis;
  }
  delete[] nbins;
  delete[] xmin;
  delete[] xmax;
  delete cAxes;
}
NBinningDef::~NBinningDef()
{
  ///
  /// Destructor
  ///
  delete fContent;
}

void NBinningDef::Print(Option_t * option) const
{
  /// Print the binning definition
  ///

  NLogInfo("NBinningDef: name='%s' %zu axes with %zu entries : %s", fName.c_str(), fDefinition.size(), fIds.size(),
           NUtils::GetCoordsString(fIds, -1).c_str());

  std::string axesStr;
  // loop over variable axes and build string
  for (size_t i = 0; i < fVariableAxes.size(); i++) {
    axesStr += fBinning->GetAxes()[fVariableAxes[i]]->GetName();
    if (i < fVariableAxes.size() - 1) {
      axesStr += ",";
    }
  }

  NLogInfo("             axis ids='%s' names='[%s]'", NUtils::GetCoordsString(fVariableAxes).c_str(), axesStr.c_str());

  TString opt = option;
  opt.ToUpper();
  if (opt.Contains("A")) {
    // loop over content dimensions and print name title
    for (int i = 0; i < fContent->GetNdimensions(); i++) {
      std::string name = fContent->GetAxis(i)->GetName();
      NLogInfo("    [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f base: nbins=%d", i,
               fContent->GetAxis(i)->GetName(), fContent->GetAxis(i)->GetTitle(), fContent->GetAxis(i)->GetNbins(),
               fContent->GetAxis(i)->GetXmin(), fContent->GetAxis(i)->GetXmax(),
               fDefinition.at(fContent->GetAxis(i)->GetName())[0][0]);
    }
  }
}

Long64_t NBinningDef::GetId(size_t index) const
{
  ///
  /// Returns ID for given index
  ///

  if (index >= fIds.size()) {
    NLogError("NBinningDef::GetId: Index %d is out of range [0, %zu)", index, fIds.size());
    return -1;
  }
  return fIds[index];
}

void NBinningDef::RefreshIdsFromContent()
{
  ///
  /// Refresh IDs from content
  ///

  fIds.clear();
  Int_t * c    = new Int_t[fContent->GetNdimensions()];
  auto    task = [this, c](const std::vector<int> & coords) {
    NLogTrace("NBinningDef::RefreshIdsFromContent: Processing coordinates %s", NUtils::GetCoordsString(coords).c_str());

    for (int i = 0; i < fContent->GetNdimensions(); i++) {
      c[i] = coords[i];
    }

    Long64_t id = fContent->GetBinContent(c);
    if (id > 0) {
      NLogTrace("NBinningDef::RefreshIdsFromContent: -> Bin content: %lld", id - 1);
      fIds.push_back(id - 1);
    }
  };

  std::vector<int> mins(fContent->GetNdimensions(), 1);
  std::vector<int> maxs(fContent->GetNdimensions());
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    TAxis * axis = fContent->GetAxis(i);
    NLogTrace("NBinningDef::RefreshIdsFromContent: Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i,
              axis->GetName(), axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    maxs[i] = axis->GetNbins();
  }

  NDimensionalExecutor executor(mins, maxs);
  executor.Execute(task);
  delete[] c;
}

void NBinningDef::RefreshContentFromIds()
{
  ///
  /// Refresh content from IDs
  ///

  // print all ids
  NLogTrace("NBinningDef::RefreshContentfomIds: Refreshing content from %zu IDs: %s", fIds.size(),
            NUtils::GetCoordsString(fIds, -1).c_str());

  fContent->Reset();
  // loop over all ids and set content
  Long64_t id;
  for (size_t i = 0; i < fIds.size(); ++i) {
    id = fIds[i];
    fBinning->GetPoint()->SetPointContentFromLinearIndex(id);
    fContent->SetBinContent(fBinning->GetPoint()->GetStorageCoords(), id + 1);
  }
  Int_t *               c = new Int_t[fContent->GetNdimensions()];
  std::vector<Long64_t> newIds;
  fIds.clear();
  auto task = [this, &newIds, c](const std::vector<int> & coords) {
    NLogTrace("NBinningDef::RefreshContentfomIds: Processing coordinates %s", NUtils::GetCoordsString(coords).c_str());

    for (int i = 0; i < fContent->GetNdimensions(); i++) {
      c[i] = coords[i];
    }

    Long64_t id = fContent->GetBinContent(c);

    // FIXME: This is a workaround to skip empty bins after first filled one
    // if (id == 0 && fIds.size() > 0) return; // skip empty bins after first filled one
    // it should be ok now
    if (id > 0) {
      NLogTrace("NBinningDef::RefreshContentfomIds: -> Bin content: %lld", id - 1);
      fIds.push_back(id - 1);
    }
  };

  std::vector<int> mins(fContent->GetNdimensions(), 1);
  std::vector<int> maxs(fContent->GetNdimensions());
  for (int i = 0; i < fContent->GetNdimensions(); i++) {
    TAxis * axis = fContent->GetAxis(i);
    NLogTrace("NBinningDef::RefreshContentfomIds: Axis %d: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i,
              axis->GetName(), axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    maxs[i] = axis->GetNbins();
  }

  NDimensionalExecutor executor(mins, maxs);
  executor.Execute(task);
  delete[] c;
}
} // namespace Ndmspc
