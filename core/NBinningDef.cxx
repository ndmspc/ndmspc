#include "NBinningDef.h"
#include "NLogger.h"
#include "NUtils.h"
#include "NBinning.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NBinningDef);
/// \endcond

namespace Ndmspc {
NBinningDef::NBinningDef(std::map<std::string, std::vector<std::vector<int>>> definition, NBinning * binning)
    : TObject(), fDefinition(definition)
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
    // Ndmspc::NLogger::Debug("Axis %zu: name='%s' title='%s' nbins=%d min=%.3f max=%.3f", i, axisName.c_str(),
    //                        axis->GetTitle(), axis->GetNbins(), axis->GetXmin(), axis->GetXmax());
    if (fDefinition[axisName].empty()) {
      SetAxisDefinition(axisName, {{axis->GetNbins()}});
    }
  }

  fAxes = new TObjArray();

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
      NLogger::Trace("  %s", NUtils::GetCoordsString(v, -1).c_str());

      int n = v.size() > 1 ? v[1] : axis->GetNbins() / v[0];
      for (int i = 0; i < n; i++) {
        iBin += v[0];
        bins[count++] = axis->GetBinUpEdge(iBin);
      }
    }
    // loop over bins and print
    for (int i = 0; i < count; i++) {
      NLogger::Trace("  %s: %d %f", axis->GetName(), i + 1, bins[i]);
    }
    axisNew->Set(count - 1, bins);
    fAxes->Add(axisNew);
  }
}
NBinningDef::~NBinningDef()
{
  ///
  /// Destructor
  ///
  delete fAxes;
}

void NBinningDef::Print(Option_t * option) const
{
  /// Print the binning definition
  ///

  NLogger::Info("NBinningDef: %zu definitions with %zu entries", fDefinition.size(), fIds.size());
  for (const auto & kv : fDefinition) {
    NLogger::Info("Binning '%s':", kv.first.c_str());
    for (const auto & v : kv.second) {
      NLogger::Info("  %s", NUtils::GetCoordsString(v, -1).c_str());
    }
  }

  // Print ids via NUtils
  NLogger::Info("Binning IDs: %s", NUtils::GetCoordsString(fIds, -1).c_str());
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
