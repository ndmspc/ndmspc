#include <algorithm>
#include <cstdio>
#include <sstream>

#include "NCombo.h"
#include "NLogger.h"
#include "NReshapeCache.h"
#include "NUtils.h"
#include "NGnNavigator.h"
#include "NGnTree.h"

namespace Ndmspc {

namespace {

/// The cache key for one combination of a tree.
NReshapeCache::Key KeyFor(NGnTree * tree, const std::vector<std::vector<int>> & levels, const std::string & binning)
{
  NReshapeCache::Key key;
  key.FromTree(tree);
  key.binning = binning.empty() ? tree->GetBinning()->GetCurrentDefinitionName() : binning;
  key.levels  = levels;
  return key;
}

} // namespace

bool NCombo::ParseLevels(const std::string & text, std::vector<std::vector<int>> & levels)
{
  levels.clear();
  if (text.empty()) return false;

  // The JSON form first, since it is what the API and the docs use.
  if (text.find('[') != std::string::npos) {
    json parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return false;
    try {
      levels = parsed.get<std::vector<std::vector<int>>>();
    }
    catch (const std::exception &) {
      return false;
    }
    return !levels.empty();
  }

  // The plain form: `:` between layers, `,` between axes — the same spelling `stress --axes` uses, so one
  // habit covers both.
  std::stringstream ss(text);
  std::string       layer;
  while (std::getline(ss, layer, ':')) {
    if (layer.empty()) continue;
    std::vector<int>          axes;
    std::stringstream         ls(layer);
    std::string               axis;
    while (std::getline(ls, axis, ',')) {
      if (axis.empty()) continue;
      if (axis.find_first_not_of("0123456789") != std::string::npos) return false;
      axes.push_back(std::stoi(axis));
    }
    if (axes.empty()) return false;
    levels.push_back(axes);
  }
  return !levels.empty();
}

std::string NCombo::DefaultLevels(NGnTree * tree)
{
  if (tree == nullptr || tree->GetBinning() == nullptr) return "[[0]]";

  const size_t nAxes = tree->GetBinning()->GetAxes().size();
  if (nAxes < 2) return "[[0]]";

  const size_t      firstLayer = std::max<size_t>(1, nAxes / 2);
  std::stringstream ss;
  ss << "[[";
  for (size_t i = 0; i < firstLayer; i++) ss << (i > 0 ? "," : "") << i;
  ss << "],[";
  for (size_t i = firstLayer; i < nAxes; i++) ss << (i > firstLayer ? "," : "") << i;
  ss << "]]";
  return ss.str();
}

bool NCombo::Store(NGnTree * tree, const std::vector<std::vector<std::vector<int>>> & levels,
                   const std::string & binning, bool withMap, bool refresh, std::vector<Result> & results)
{
  results.clear();
  if (tree == nullptr || tree->IsZombie()) {
    NLogError("NCombo: no tree to reshape");
    return false;
  }
  if (levels.empty()) {
    NLogError("NCombo: no combinations were given");
    return false;
  }

  bool ok = true;
  for (const auto & combination : levels) {
    if (combination.empty()) {
      NLogError("NCombo: a combination has no axes");
      ok = false;
      continue;
    }

    Result      result;
    result.levels = combination;
    const auto key = KeyFor(tree, combination, binning);
    result.digest  = NReshapeCache::Digest(key);

    // Already stored: leave it alone unless a rebuild was asked for. Rebuilding a good entry costs the
    // reshape it was meant to save, so it must be opt-in.
    if (!refresh) {
      if (NGnNavigator * existing = NReshapeCache::Load(key, tree)) {
        result.already  = true;
        result.children = existing->GetChildren().size();
        result.nLevels  = existing->GetNLevels();
        delete existing;
      }
    }

    if (!result.already) {
      NGnNavigator * nav = tree->Reshape(key.binning, combination, 0, {}, {});
      if (nav == nullptr) {
        // A combination the tree cannot take (levels that miss an axis, or name one twice) fails here, and
        // must not be reported as stored.
        NLogError("NCombo: '%s' could not be reshaped", json(combination).dump().c_str());
        ok = false;
        continue;
      }
      result.children = nav->GetChildren().size();
      result.nLevels  = nav->GetNLevels();

      result.stored = NReshapeCache::Store(key, nav);
      if (!result.stored) {
        NLogError("NCombo: '%s' could not be stored", json(combination).dump().c_str());
        ok = false;
      }
      delete nav;
    }

    // The map JSON is what a map request otherwise exports, so keeping it makes the first map a file read.
    // Only for a stored combination: there is nothing to hang it on otherwise.
    if (withMap && (result.stored || result.already)) {
      NGnNavigator * nav = NReshapeCache::Load(key, tree);
      if (nav != nullptr) {
        json nested;
        json cfg;
        cfg["averages"] = nav->GetAverageParameters();
        nav->ExportToJson(nested, nav, std::vector<std::string>{}, cfg);
        if (!nested.empty()) result.mapStored = NReshapeCache::StoreMapJson(key, nested.dump());
        delete nav;
      }
    }

    results.push_back(result);
  }
  return ok;
}

std::string NCombo::List(const std::string & file)
{
  return NReshapeCache::List(file);
}

} // namespace Ndmspc
