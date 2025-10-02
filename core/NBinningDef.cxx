#include "NBinningDef.h"
#include "TObjArray.h"
#include "TObject.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NBinning.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NBinningDef);
/// \endcond

namespace Ndmspc {
NBinningDef::NBinningDef(std::string name, std::map<std::string, std::vector<std::vector<int>>> definition,
                         NBinning * binning)
    : TObject(), fName(name), fDefinition(definition)
{
  ///
  /// Constructor
  ///

  // TODO: build vector of TAxis from the definition
  // Print definition
  // for (const auto & kv : fDefinition) {
  //   NLogger::Debug("NBinningDef: Binning '%s': %zu entries", kv.first.c_str(), kv.second.size());
  //   for (const auto & v : kv.second) {
  //     NLogger::Debug("  %s", NUtils::GetCoordsString(v, -1).c_str());
  //   }
  // }

  if (binning == nullptr) {
    // NLogger::Error("NBinningDef: Binning is nullptr");
    return;
  }

  std::vector<TAxis *> axes = binning->GetAxes();
  for (size_t i = 0; i < axes.size(); i++) {
    TAxis *     axis     = axes[i];
    std::string axisName = axis->GetName();
    // Ndmspc::NLogger::Debug("NBinningDef::NBinningDef: Axis %zu: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i,
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
  for (int i = 0; i < axes.size(); i++) {
    TAxis * axis    = (TAxis *)axes[i];
    TAxis * axisNew = (TAxis *)axis->Clone();

    std::string name = axis->GetName();
    NLogger::Trace("NBinningDef: Binning '%s': %d", name.c_str(), axis->GetNbins());

    double bins[axis->GetNbins() + 1];
    int    count  = 0;
    bins[count++] = axis->GetBinLowEdge(1);

    int                                                  iBin       = 0;
    std::map<std::string, std::vector<std::vector<int>>> definition = GetDefinition();
    for (auto & v : definition.at(name)) {
      // NLogger::Debug("  %s", NUtils::GetCoordsString(v, -1).c_str());

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
    //   NLogger::Debug("  %s: %d %f", axis->GetName(), i + 1, bins[i]);
    // }
    axisNew->Set(count - 1, bins);
    cAxes->Add(axisNew);
  }

  for (int i = 0; i < cAxes->GetEntries(); i++) {
    TAxis * axis = (TAxis *)cAxes->At(i);
    nbins[i]     = axis->GetNbins();
    xmin[i]      = 0;
    xmax[i]      = axis->GetNbins();
  }
  fContent = new THnSparseL("content", "content", dims, nbins, xmin, xmax);

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

    if (axis->IsAlphanumeric()) {
      for (int b = 1; b <= axis->GetNbins(); b++) {
        axisContent->SetBinLabel(b, axis->GetBinLabel(b));
      }
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

  NLogger::Info("NBinningDef: name='%s' %zu definitions with %zu entries : %s", fName.c_str(), fDefinition.size(),
                fIds.size(), NUtils::GetCoordsString(fIds, -1).c_str());

  TString opt = option;
  opt.ToUpper();
  if (opt.Contains("A")) {
    // loop over content dimensions and print name title
    for (int i = 0; i < fContent->GetNdimensions(); i++) {
      std::string name = fContent->GetAxis(i)->GetName();
      NLogger::Info("    [%d] name='%s' title='%s' nbins=%d min=%.3f max=%.3f base: nbins=%d", i,
                    fContent->GetAxis(i)->GetName(), fContent->GetAxis(i)->GetTitle(), fContent->GetAxis(i)->GetNbins(),
                    fContent->GetAxis(i)->GetXmin(), fContent->GetAxis(i)->GetXmax(),
                    fDefinition.at(fContent->GetAxis(i)->GetName())[0][0]);
    }
  }
}

Long64_t NBinningDef::GetId(int index) const
{
  ///
  /// Returns ID for given index
  ///

  if (index < 0 || index >= static_cast<int>(fIds.size())) {
    NLogger::Error("NBinningDef::GetId: Index %d is out of range [0, %zu)", index, fIds.size());
    return -1;
  }
  return fIds[index];
}

} // namespace Ndmspc
