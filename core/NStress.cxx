#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

#include <TF1.h>
#include <TFitResult.h>
#include <TH1D.h>
#include <TObjArray.h>
#include <TSystem.h>

#include "NLogger.h"
#include "NParameters.h"
#include "NReshapeCache.h"
#include "NStress.h"
#include "NUtils.h"
#include "NGnNavigator.h"
#include "NGnTree.h"

/// \cond CLASSIMP
ClassImp(Ndmspc::NStress);
/// \endcond

namespace Ndmspc {

namespace {

/** How many entries the synthetic source fills per object. A function pointer cannot capture. */
int gStressEntries = 20000;

/**
 * Fill one point of the synthetic source with a fitted histogram.
 *
 * `AddToGlobalList(false)` on the fit function is not cosmetic: a `TF1` left on ROOT's global list is what
 * made multi-worker runs crash before (the root cause of a `TPad`/drain crash).
 */
void FillPoint(NBinningPoint * point, TList * /*output*/, TList * outputPoint, int /*threadId*/)
{
  if (point == nullptr || outputPoint == nullptr) return;

  TH1D * histogram = new TH1D("hGaus", "g", 200, -10, 10);
  histogram->FillRandom("gaus", gStressEntries);

  TF1 * gaus = new TF1("gausFunc", "gaus", -10, 10);
  gaus->AddToGlobalList(false);
  TFitResultPtr fit = histogram->Fit(gaus, "QS");
  if (point->GetParameters() != nullptr) {
    point->GetParameters()->SetParameter("meanFit", fit->Parameter(1), fit->Error(1));
  }
  outputPoint->Add(histogram);
}

/**
 * The copy lambda.
 *
 * A bare function pointer, so it cannot capture: everything comes from `point->GetCfg()`, the same way
 * `NGnTree::ImportNgnt` builds its per-point filename. The progress bar is off because several workers
 * write to one console — the interleaving the logger already guards against.
 */
void CopyPoint(NBinningPoint * point, TList * /*output*/, TList * /*outputPoint*/, int /*threadId*/)
{
  if (point == nullptr) return;
  const json & cfg = point->GetCfg();
  if (!cfg.contains("source") || !cfg.contains("workdir")) return;

  std::string destination = cfg["workdir"].get<std::string>();
  for (const auto & axisName : cfg.value("axisNames", std::vector<std::string>{})) {
    destination += "/" + std::string(point->GetBinLabel(axisName));
  }
  destination += "/" + cfg.value("basename", std::string("d.root"));

  // Never a link: an in-file navigator written into one bin would then exist for every bin, and the
  // bins-are-independent measurement would mean nothing.
  //
  // `TFile::Cp` (and so `NUtils::Cp`) returns **1 on success**, not 0, so this tests `<= 0` — an
  // `!= 0` check logs an error for every copy that worked.
  if (NUtils::Cp(cfg["source"].get<std::string>(), destination, kFALSE) <= 0) {
    NLogError("NStress: cannot copy into '%s'", destination.c_str());
  }
}

/// A scoped environment setting, so a phase cannot leak its setting into the next one.
class ScopedEnv {
  public:
  /// Set `name` to `value`, remembering what it was.
  ScopedEnv(const char * name, const std::string & value) : fName(name)
  {
    const char * previous = gSystem->Getenv(name);
    if (previous != nullptr) {
      fHad = true;
      fWas = previous;
    }
    gSystem->Setenv(name, value.c_str());
  }
  ~ScopedEnv()
  {
    if (fHad) gSystem->Setenv(fName.c_str(), fWas.c_str());
    else gSystem->Unsetenv(fName.c_str());
  }
  ScopedEnv(const ScopedEnv &)            = delete;
  ScopedEnv & operator=(const ScopedEnv &) = delete;

  private:
  std::string fName;
  std::string fWas;
  bool        fHad{false};
};

/**
 * Reshape one combination the way the reshape handler does.
 *
 * The cache lives in the tool handler, not in `NGnTree::Reshape`, so this drives the same load/store pair.
 * Calling `Reshape` alone would measure the uncached path every time and report no speed-up at all.
 */
NGnNavigator * ReshapeOnce(NGnTree * tree, const NReshapeCache::Key & key, const std::vector<std::vector<int>> & levels)
{
  NGnNavigator * nav = NReshapeCache::Load(key, tree);
  if (nav != nullptr) return nav;

  nav = tree->Reshape(key.binning, levels, 0, {}, {});
  if (nav != nullptr) NReshapeCache::Store(key, nav);
  return nav;
}

/// The numbers for one (run, phase) point. A function pointer cannot capture, so they travel here.
std::map<std::string, std::pair<double, double>> gStressResults;

/**
 * Fill one point of the results tree.
 *
 * The point's labels say which run and phase it is, so the lookup key is built from them — that is what
 * makes the tree an ordinary importable one rather than a container of opaque objects.
 *
 * `point->AddLog` is what gives the point an object: `NGnThreadData::Process` attaches the log to the
 * output itself after the function returns (`core/NGnThreadData.cxx:257-261`), so no object is added here.
 * Setting parameters alone would write no entry at all and leave the tree empty.
 */
void FillResult(NBinningPoint * point, TList * /*output*/, TList * /*outputPoint*/, int /*threadId*/)
{
  if (point == nullptr || point->GetParameters() == nullptr) return;

  const std::string key = std::string(point->GetBinLabel("run")) + "|" + std::string(point->GetBinLabel("phase"));
  const auto        it  = gStressResults.find(key);
  if (it == gStressResults.end()) return;

  const double seconds = it->second.first;
  const double ratio   = it->second.second;

  point->GetParameters()->SetParameter("seconds", seconds);
  point->GetParameters()->SetParameter("ratio", ratio);

  point->AddLog("**%s / %s**", std::string(point->GetBinLabel("run")).c_str(),
                std::string(point->GetBinLabel("phase")).c_str());
  point->AddLog("- **seconds**: %.4f s", seconds);
  point->AddLog("- **ratio**: %.3f of the cold baseline", ratio);
}

} // namespace

NStress::NStress()  = default;
NStress::~NStress() = default;

double NStress::Sample::Median() const
{
  if (seconds.empty()) return 0.0;
  std::vector<double> sorted = seconds;
  std::sort(sorted.begin(), sorted.end());
  return sorted[sorted.size() / 2];
}

double NStress::Sample::Best() const
{
  return seconds.empty() ? 0.0 : *std::min_element(seconds.begin(), seconds.end());
}

std::string NStress::RunResult::PhaseName(Phase phase) const
{
  switch (phase) {
  case kColdCacheOff: return "cold cache-off";
  case kColdCacheOn: return "cold cache-on";
  case kWarmCacheHit: return "warm cache-hit";
  default: return "unknown";
  }
}

std::vector<NStress::AxisSpec> NStress::ParseAxes() const
{
  // Two separators, each with one job:
  //
  //   `:`  separates **axes**            a1=x,y:a2=3:a3=alpha,beta
  //   `,`  separates **labels** of an axis
  //
  // That is unambiguous, which the old single-comma form was not: there a comma meant both, and
  // `a1=x,a2=2` had to be disambiguated by looking for `=`. A bare number after `=` is still a **count** of
  // numbered bins (`a4=2` is `1,2`), so a numeric axis needs no list.
  //
  // The single-comma form is still accepted when the text has no `:` at all, because it is what every
  // command in the docs and shell history uses; the `:` form is preferred for anything new.
  const bool colonForm = fAxes.find(':') != std::string::npos;

  std::vector<std::string> specs;
  {
    std::stringstream ss(fAxes);
    std::string       token;
    while (std::getline(ss, token, colonForm ? ':' : ',')) {
      if (!token.empty()) specs.push_back(token);
    }
  }

  std::vector<AxisSpec> axes;
  if (colonForm) {
    for (const std::string & spec : specs) {
      const size_t equals = spec.find('=');
      if (equals == std::string::npos || equals == 0) return {};

      AxisSpec    axis;
      axis.name = spec.substr(0, equals);
      {
        // Everything after `=` is this axis's labels, comma separated.
        std::stringstream ls(spec.substr(equals + 1));
        std::string       label;
        while (std::getline(ls, label, ',')) {
          if (!label.empty()) axis.labels.push_back(label);
        }
      }
      axes.push_back(axis);
    }
  }
  else {
    // Legacy: a token with `=` starts an axis, one without is a label of the axis before it.
    for (const std::string & token : specs) {
      const size_t equals = token.find('=');
      if (equals != std::string::npos) {
        AxisSpec          axis;
        const std::string labels = token.substr(equals + 1);
        axis.name                = token.substr(0, equals);
        if (axis.name.empty()) return {};
        if (!labels.empty()) axis.labels.push_back(labels);
        axes.push_back(axis);
      }
      else {
        if (axes.empty()) return {};
        axes.back().labels.push_back(token);
      }
    }
  }

  for (auto & axis : axes) {
    // A single bare number means a count of numbered bins, which is what `a4=2` reads as.
    if (axis.labels.size() == 1 && axis.labels[0].find_first_not_of("0123456789") == std::string::npos) {
      const int count = std::stoi(axis.labels[0]);
      if (count <= 0) return {};
      axis.labels.clear();
      for (int i = 1; i <= count; i++) axis.labels.push_back(std::to_string(i));
    }
    if (axis.labels.empty()) return {};
  }
  return axes;
}

std::string NStress::ResolveSource()
{
  if (!fSource.empty()) return fSource;

  const std::string generated = fWorkdir + "/source.root";
  if (!fForce && gSystem->AccessPathName(generated.c_str()) == kFALSE) {
    // Remember it, exactly as the generating path below does: returning the name without recording it
    // leaves `fSource` empty, and the copy phase then reads an empty source and copies nothing.
    fSource = generated;
    return generated;
  }

  NLogPrint("ndmspc-cli: source       : generating '%s' (%d bins, %d entries)", generated.c_str(), fBins, fEntries);

  std::vector<std::string> labels;
  for (int i = 1; i <= fBins; i++) labels.push_back(std::to_string(i));

  TObjArray * axes = new TObjArray();
  axes->Add(NUtils::CreateAxisFromLabels("b1", "b1", labels));
  axes->Add(NUtils::CreateAxisFromLabels("b2", "b2", labels));

  NGnTree * ngnt = new NGnTree(axes, generated);
  if (ngnt->IsZombie()) {
    delete ngnt;
    return "";
  }
  // A labelled axis takes one single-bin definition meaning "the whole axis": NBinning expands the labels
  // itself and rejects anything else on an alphanumeric axis.
  std::map<std::string, std::vector<std::vector<int>>> defs;
  defs["b1"] = {{1}};
  defs["b2"] = {{1}};
  ngnt->GetBinning()->AddBinningDefinition("default", defs);
  ngnt->InitParameters({"meanFit"});

  gStressEntries   = fEntries;
  const bool built = ngnt->Process(FillPoint, json::object(), "default");
  delete ngnt;

  if (!built) {
    NLogError("NStress: generating the synthetic source failed");
    return "";
  }
  fSource = generated;
  return generated;
}

bool NStress::Build()
{
  const std::vector<AxisSpec> axes = ParseAxes();
  if (axes.empty()) {
    // Said twice on purpose: the CLI defaults to run-only logging, so an NLogError alone would go to the
    // log file and a user who mistyped `--axes` would see nothing but a non-zero exit.
    const std::string why = "--axes is unusable ('" + fAxes +
                            "') — expected NAME=COUNT or NAME=label1,label2, with ':' between axes "
                            "(e.g. a1=x,y:a2=3)";
    NLogError("NStress: %s", why.c_str());
    NLogPrint("NStress: %s", why.c_str());
    return false;
  }
  if (fWorkdir.empty()) {
    NLogError("NStress: a workdir is required");
    return false;
  }

  // Clear first, then resolve the source — the order matters. A generated source lives **inside** the
  // workdir, so clearing afterwards deletes the very file the copies are made from, and every copy then
  // fails against a source that was there a moment ago. `NUtils::Rm` handles a local or remote workdir.
  //
  // The guard matters more than the removal: this deletes whatever `--workdir` names, so a path that would
  // take the file system, the current directory or a home directory with it is refused. Anything deeper —
  // absolute or relative, `eos`, `out/run1` — is a workdir and is cleared.
  if (fForce) {
    std::string trimmed = fWorkdir;
    while (trimmed.size() > 1 && trimmed.back() == '/') trimmed.pop_back();

    const bool remotePath = trimmed.find("://") != std::string::npos;
    const bool tooBroad   = !remotePath && (trimmed.empty() || trimmed == "." || trimmed == ".." ||
                                            trimmed == "/" || trimmed == "~" || trimmed == gSystem->pwd());
    if (tooBroad) {
      NLogError("NStress: refusing to clear '%s' — it is not a directory of its own", fWorkdir.c_str());
      return false;
    }
    NLogPrint("ndmspc-cli: clearing     : '%s' (--force)", trimmed.c_str());
    NUtils::Rm(trimmed, true);
    // The source may have been the file just removed, so a generated one is resolved afresh below.
    if (!fSourceGiven) fSource.clear();
  }

  if (ResolveSource().empty()) return false;

  long long copies = 1;
  for (const auto & axis : axes) copies *= static_cast<long long>(axis.labels.size());

  // A remote workdir (`root://`, `http://`, …) needs a different probe: `gSystem->GetPathInfo` only knows
  // local paths, so it reports nothing and every size below would silently be zero.
  const bool remoteWorkdir = fWorkdir.find("://") != std::string::npos;

  long long sourceBytes = 0;
  if (remoteWorkdir) {
    TFile * source = TFile::Open(fSource.c_str());
    if (source != nullptr && !source->IsZombie()) sourceBytes = static_cast<long long>(source->GetSize());
    if (source != nullptr) {
      source->Close();
      delete source;
    }
  }
  else {
    FileStat_t st;
    if (gSystem->GetPathInfo(fSource.c_str(), st) == 0) sourceBytes = static_cast<long long>(st.fSize);
  }
  const long long needed = copies * sourceBytes;

  NLogPrint("ndmspc-cli: copies       : %lld (%lld bytes, ~%.1f MB) to %s", copies, needed, needed / 1048576.0,
            remoteWorkdir ? "a remote workdir" : fWorkdir.c_str());
  if (fMaxBytes >= 0 && needed > fMaxBytes) {
    NLogError("NStress: the layout needs %lld bytes, above the limit of %lld", needed, fMaxBytes);
    return false;
  }

  // The skeleton first, on this thread — for a **local** workdir only. A remote one needs no mkdir at all:
  // ROOT's `TFile::Open(..., "RECREATE")` creates the parent directories on `root://` itself (verified),
  // and `gSystem`/`NUtils::CreateDirectory` cannot make remote directories (CreateDirectory even returns
  // true for a remote path while doing nothing, which is why this must not rely on it).
  std::function<bool(size_t, const std::string &)> makeDirs = [&](size_t level, const std::string & prefix) {
    if (remoteWorkdir) return true;
    if (level == axes.size()) return true;
    for (const auto & label : axes[level].labels) {
      const std::string dir = prefix + "/" + label;
      if (gSystem->AccessPathName(dir.c_str(), kWritePermission)) {
        if (!NUtils::CreateDirectory(dir) || gSystem->AccessPathName(dir.c_str(), kWritePermission)) {
          NLogError("NStress: cannot create '%s'", dir.c_str());
          return false;
        }
      }
      if (!makeDirs(level + 1, dir)) return false;
    }
    return true;
  };
  if (!makeDirs(0, fWorkdir)) return false;

  // The copy tree: the grid's axes, one bin per label, no data. `Process` iterates the binning
  // definitions rather than the entries, so a tree with no data still visits every cell.
  TObjArray * axisArray = new TObjArray();
  for (const auto & axis : axes) {
    axisArray->Add(NUtils::CreateAxisFromLabels(axis.name, axis.name, axis.labels));
  }
  NGnTree * grid = new NGnTree(axisArray, fWorkdir + "/copy-grid.root");
  if (grid->IsZombie()) {
    delete grid;
    NLogError("NStress: cannot build the copy tree");
    return false;
  }
  std::map<std::string, std::vector<std::vector<int>>> defs;
  std::vector<std::string>                            axisNames;
  for (const auto & axis : axes) {
    defs[axis.name] = {{1}};
    axisNames.push_back(axis.name);
  }
  grid->GetBinning()->AddBinningDefinition("default", defs);

  json cfg;
  cfg["source"]    = fSource;
  cfg["workdir"]   = fWorkdir;
  cfg["basename"]  = fBasename;
  cfg["axisNames"] = axisNames;

  NLogPrint("ndmspc-cli: mode         : %s",
            gSystem->Getenv("NDMSPC_EXECUTION_MODE") != nullptr ? gSystem->Getenv("NDMSPC_EXECUTION_MODE") : "serial");

  const double started = NUtils::Now();
  const bool   copied  = grid->Process(CopyPoint, cfg);
  const double elapsed = NUtils::Since(started);
  delete grid;

  if (!copied) {
    NLogError("NStress: the copy phase failed");
    return false;
  }
  NLogPrint("ndmspc-cli: copied       : %lld files in %.1f s (%.1f MB/s)", copies, elapsed,
            elapsed > 0 ? needed / elapsed / 1048576.0 : 0.0);
  return true;
}

bool NStress::Verify()
{
  const std::vector<AxisSpec> axes = ParseAxes();
  if (axes.empty() || axes[0].labels.size() < 2) {
    NLogPrint("ndmspc-cli: verify       : skipped (one bin on the first axis)");
    return true;
  }

  // The device/inode check is what catches a layout whose bins are links, and it is local-only: a remote
  // file has no inode `gSystem` can report. For a remote workdir the copies are checked for size only, and
  // the link check is skipped rather than guessed at — reporting "same file" from two zeroed stats would
  // fail every remote run, and reporting "fine" from them would be a lie.
  const bool remote = fWorkdir.find("://") != std::string::npos;

  // Vary **one** axis at a time from the first bin of every other: the pair a reader would compare, and
  // the one a stray link is most likely to join. Varying every axis at once would miss a link between
  // two neighbours, which is exactly how a mistake in the copy phase would show up.
  auto pathOf = [&](const std::vector<std::string> & labels) {
    std::string path = fWorkdir;
    for (size_t i = 0; i < axes.size() && i < labels.size(); i++) path += "/" + labels[i];
    return path + "/" + fBasename;
  };

  /// The size of a copy, and its identity when it can be known (local only).
  struct CopyState {
    long long size{-1};
    bool      hasIdentity{false};
    Int_t     dev{0};
    Long64_t  ino{0};
  };
  auto stateOf = [&](const std::string & path) -> CopyState {
    CopyState state;
    if (remote) {
      TFile * file = TFile::Open(path.c_str());
      if (file != nullptr && !file->IsZombie()) {
        state.size = static_cast<long long>(file->GetSize());
        state.hasIdentity = true;
      }
      if (file != nullptr) {
        file->Close();
        delete file;
      }
      return state;
    }

    FileStat_t stat;
    if (gSystem->GetPathInfo(path.c_str(), stat) != 0) return state;
    state.size        = static_cast<long long>(stat.fSize);
    state.hasIdentity = true;
    state.dev         = stat.fDev;
    state.ino         = stat.fIno;
    return state;
  };

  std::vector<std::string> baseLabels;
  for (const auto & axis : axes) baseLabels.push_back(axis.labels[0]);

  const std::string base     = pathOf(baseLabels);
  const CopyState   baseStat = stateOf(base);
  if (baseStat.size < 0) {
    NLogError("NStress: a copy is missing (%s)", base.c_str());
    return false;
  }

  for (size_t i = 0; i < axes.size(); i++) {
    std::vector<std::string> other = baseLabels;
    other[i]                       = axes[i].labels[1];

    const std::string path = pathOf(other);
    const CopyState   stat = stateOf(path);
    if (stat.size < 0) {
      NLogError("NStress: a copy is missing (%s)", path.c_str());
      return false;
    }
    // Device + inode identify a file: equal means the two paths are one file, i.e. a link. Local only.
    if (!remote && stat.hasIdentity && baseStat.hasIdentity && stat.dev == baseStat.dev && stat.ino == baseStat.ino) {
      NLogError("NStress: '%s' and '%s' are the same file — the copies are links, so the grid is not "
                "independent and the measurement means nothing",
                base.c_str(), path.c_str());
      return false;
    }
    if (stat.size != baseStat.size) {
      NLogError("NStress: the copies differ in size ('%s' %lld vs '%s' %lld)", base.c_str(), baseStat.size,
                path.c_str(), stat.size);
      return false;
    }
  }

  NLogPrint("ndmspc-cli: verify       : same size (%lld bytes), %s", baseStat.size,
            remote ? "link check skipped for a remote workdir" : "separate files");
  return true;
}

std::string NStress::DefaultLevels(NGnTree * tree)
{
  if (tree == nullptr || tree->GetBinning() == nullptr) return "[[0]]";

  const size_t nAxes = tree->GetBinning()->GetAxes().size();
  if (nAxes < 2) return "[[0]]";

  // `Reshape` requires the levels to cover every variable axis, so a fixed default is only right for one
  // tree shape; this builds one from the tree in front of it.
  const size_t      firstLayer = std::max<size_t>(1, nAxes / 2);
  std::stringstream ss;
  ss << "[[";
  for (size_t i = 0; i < firstLayer; i++) ss << (i > 0 ? "," : "") << i;
  ss << "],[";
  for (size_t i = firstLayer; i < nAxes; i++) ss << (i > firstLayer ? "," : "") << i;
  ss << "]]";
  return ss.str();
}

bool NStress::Run(const std::string & label)
{
  const std::vector<AxisSpec> axes = ParseAxes();
  if (axes.empty()) {
    // Said twice on purpose: the CLI defaults to run-only logging, so an NLogError alone would go to the
    // log file and a user who mistyped `--axes` would see nothing but a non-zero exit.
    const std::string why = "--axes is unusable ('" + fAxes +
                            "') — expected NAME=COUNT or NAME=label1,label2, with ':' between axes "
                            "(e.g. a1=x,y:a2=3)";
    NLogError("NStress: %s", why.c_str());
    NLogPrint("NStress: %s", why.c_str());
    return false;
  }
  if (fWorkdir.empty()) {
    NLogError("NStress: a workdir is required");
    return false;
  }
  if (ResolveSource().empty()) return false;

  // Import the layout, and check it before measuring: a tree that cannot be imported, or whose grid axes
  // are not the copy tree's, would be measured as an empty or mislabelled thing.
  std::vector<std::string> headers;
  long long                gridPoints = 1;
  for (const auto & axis : axes) {
    headers.push_back(axis.name);
    gridPoints *= static_cast<long long>(axis.labels.size());
  }

  json cfg;
  cfg["type"]       = "ngnt";
  cfg["basedir"]    = fWorkdir;
  cfg["filename"]   = fBasename;
  // `Import()` reads `axes`, not `headers` (that name is internal to ImportNgnt). With an empty list
  // AxesFromDirectory silently skips every path and the import produces no points at all.
  cfg["axes"]       = headers;
  const std::string importedFile = fWorkdir + "/imported.root";
  cfg["outputFile"] = importedFile;

  // Reuse an existing import unless asked to rebuild. Importing again **rewrites** this file, and the
  // cache entries for its combinations live inside it — so re-importing would wipe the very entries a
  // second run is meant to find, and every run would honestly (but uselessly) measure a cold cache.
  NGnTree * tree = nullptr;
  if (!fForce && gSystem->AccessPathName(importedFile.c_str()) == kFALSE) {
    tree = NGnTree::Open(importedFile);
    if (tree != nullptr && tree->IsZombie()) {
      delete tree;
      tree = nullptr;
    }
  }
  if (tree != nullptr) {
    NLogPrint("ndmspc-cli: imported     : reused '%s' (pass --force to import again)", importedFile.c_str());
  }
  else {
    tree = NGnTree::Import(cfg);
  }
  if (tree == nullptr) {
    NLogError("NStress: the layout cannot be imported");
    return false;
  }

  const auto allAxes = tree->GetBinning()->GetAxes();
  if (allAxes.size() < headers.size()) {
    NLogError("NStress: imported %zu axes, expected at least the %zu grid axes", allAxes.size(), headers.size());
    delete tree;
    return false;
  }
  // The grid axes come first: the directory-derived axes are the base and the source tree's are appended.
  std::vector<std::string> got;
  for (size_t i = 0; i < headers.size(); i++) got.push_back(allAxes[i] != nullptr ? allAxes[i]->GetName() : "");
  if (got != headers) {
    NLogError("NStress: the grid axes did not come back in the copy tree's order (got %s, expected %s)",
              json(got).dump().c_str(), json(headers).dump().c_str());
    delete tree;
    return false;
  }

  // One grid cell holds every point of the source tree it copied, so the total is the product.
  auto *          definition   = tree->GetBinning()->GetDefinition("default");
  const long long points       = definition != nullptr ? static_cast<long long>(definition->GetIds().size()) : 0;
  long long       sourcePoints = 1;
  for (size_t i = headers.size(); i < allAxes.size(); i++) {
    if (allAxes[i] != nullptr) sourcePoints *= allAxes[i]->GetNbins();
  }
  if (points != gridPoints * sourcePoints) {
    NLogError("NStress: the layout holds %lld points, expected %lld (%lld cells x %lld source points)",
              points, gridPoints * sourcePoints, gridPoints, sourcePoints);
    delete tree;
    return false;
  }
  NLogPrint("ndmspc-cli: imported     : %lld points (%lld cells x %lld), grid axes %s", points, gridPoints,
            sourcePoints, json(got).dump().c_str());

  fResolvedLevels            = fLevels.empty() ? DefaultLevels(tree) : fLevels;
  const auto levels          = json::parse(fResolvedLevels).get<std::vector<std::vector<int>>>();
  NLogPrint("ndmspc-cli: levels       : %s", fResolvedLevels.c_str());

  NReshapeCache::Key key;
  key.FromTree(tree);
  key.binning = tree->GetBinning()->GetCurrentDefinitionName();
  key.levels  = levels;

  // Entries live inside the copied files, so they survive between runs: a "cold" phase on a workdir that
  // was measured before is not cold, and saying so is better than reporting a meaningless baseline.
  if (NGnNavigator * existing = NReshapeCache::Load(key, tree)) {
    delete existing;
    NLogPrint("ndmspc-cli: note         : this combination is already stored, so the 'cold' phases below are "
                             "really cache hits — rebuild the tree for a cold baseline");
  }

  RunResult result;
  result.label   = label.empty() ? fWorkdir : label;
  result.source  = fSource;
  result.workdir = fWorkdir;

  // The cache is compared by turning it off and on for one phase at a time.
  struct PhasePlan {
    Phase       phase;
    const char * cache;
    int         repeats;
    const char * why;
  };
  const PhasePlan plans[] = {
      {kColdCacheOff, "0", fRepeat,
       "with the cache off nothing is stored, so every repeat is honestly cold"},
      {kColdCacheOn, "1", 1,
       "runs once: a second run would be a cache hit, and its median would hide the cold build entirely"},
      {kWarmCacheHit, "1", fRepeat, "by now the entry is stored, so every repeat is a hit"},
  };

  for (const auto & plan : plans) {
    Sample        sample;
    std::optional<ScopedEnv> scoped;
    if (plan.cache != nullptr) scoped.emplace("NDMSPC_RESHAPE_CACHE", plan.cache);

    for (int i = 0; i < std::max(1, plan.repeats); i++) {
      const double   started = NUtils::Now();
      NGnNavigator * nav     = ReshapeOnce(tree, key, levels);
      const double   elapsed = NUtils::Since(started);

      if (nav == nullptr) {
        // A failed reshape returns at once, so its time would read as an enormous speed-up.
        NLogError("NStress: phase '%s' could not reshape, so it has no timing (check the levels against the "
                  "imported tree's axes)",
                  result.PhaseName(plan.phase).c_str());
        sample.seconds.clear();
        sample.ran = false;
        break;
      }
      sample.seconds.push_back(elapsed);
      sample.ran = true;
      delete nav;
    }
    result.phases[plan.phase] = sample;
  }

  delete tree;
  fRuns.push_back(result);

  Print();

  // Success means the phases **ran** and produced numbers; whether the cache made it faster is the result,
  // not a pass/fail. Judging that here made `Run` flaky on a tree small enough for timer noise to outweigh
  // the saving, and it hid the measurement behind a boolean — the table and GetSpeedup() say it better.
  bool ran = true;
  for (int phase = 0; phase < kPhaseCount; phase++) {
    if (!result.phases[phase].ran || result.phases[phase].seconds.empty()) ran = false;
  }
  if (ran && GetSpeedup() <= 1.0) {
    NLogWarning("NStress: a revisit was not cheaper than a cold build (%.4f s vs %.4f s) — on a very small "
                "tree that is timer noise, on a large one it means the cache did not hit",
                GetWarm(), GetCold());
  }
  return ran;
}

double NStress::GetCold() const
{
  if (fRuns.empty()) return 0.0;
  return fRuns.back().phases[kColdCacheOff].Median();
}

double NStress::GetWarm() const
{
  if (fRuns.empty()) return 0.0;
  return fRuns.back().phases[kWarmCacheHit].Median();
}

double NStress::GetSpeedup() const
{
  const double cold = GetCold();
  const double warm = GetWarm();
  if (cold <= 0 || warm <= 0) return 0.0;
  return cold / warm;
}

NGnTree * NStress::GetResults(const std::string & outFile) const
{
  if (fRuns.empty()) {
    NLogError("NStress: GetResults: nothing has been measured");
    return nullptr;
  }

  // One point per (run, phase), so several runs — a local disk and an EOS copy — end up in one tree that
  // can be reshaped and drawn like any other. The run axis takes one bin per run label, so the points are
  // the grid of the two axes; that is what makes two runs comparable inside a single N-dimensional tree.
  std::vector<std::string> runLabels, phaseLabels;
  for (const auto & run : fRuns) {
    if (std::find(runLabels.begin(), runLabels.end(), run.label) == runLabels.end()) runLabels.push_back(run.label);
  }
  for (int phase = 0; phase < kPhaseCount; phase++) {
    phaseLabels.push_back(fRuns.front().PhaseName(static_cast<Phase>(phase)));
  }

  TObjArray * axes = new TObjArray();
  axes->Add(NUtils::CreateAxisFromLabels("run", "Run", runLabels));
  axes->Add(NUtils::CreateAxisFromLabels("phase", "Phase", phaseLabels));

  const std::string file = outFile.empty() ? "/tmp/ndmspc-stress-results.root" : outFile;
  NGnTree *         tree = new NGnTree(axes, file);
  if (tree->IsZombie()) {
    delete tree;
    return nullptr;
  }

  std::map<std::string, std::vector<std::vector<int>>> defs;
  defs["run"]   = {{1}};
  defs["phase"] = {{1}};
  tree->GetBinning()->AddBinningDefinition("default", defs);
  tree->InitParameters({"seconds", "ratio"});

  // A function pointer cannot capture, so the numbers travel in a file-scope map keyed by the point.
  gStressResults.clear();
  for (const auto & run : fRuns) {
    const double cold = run.phases[kColdCacheOff].Median();
    for (int phase = 0; phase < kPhaseCount; phase++) {
      const double seconds = run.phases[phase].Median();
      gStressResults[run.label + "|" + run.PhaseName(static_cast<Phase>(phase))] =
          std::make_pair(seconds, cold > 0 ? seconds / cold : 0.0);
    }
  }

  tree->Process(FillResult, json::object(), "default");
  return tree;
}

void NStress::Print(Option_t * /*option*/) const
{
  NLogPrint("ndmspc-cli: workdir      : %s", fWorkdir.c_str());
  NLogPrint("ndmspc-cli: axes         : %s", fAxes.c_str());
  NLogPrint("ndmspc-cli: source       : %s", fSource.empty() ? "(generated)" : fSource.c_str());
  if (!fResolvedLevels.empty()) NLogPrint("ndmspc-cli: levels       : %s", fResolvedLevels.c_str());

  for (const auto & run : fRuns) {
    NLogPrint("");
    NLogPrint("ndmspc-cli: run          : %s", run.label.c_str());
    NLogPrint("ndmspc-cli: phase             median        best");
    for (int phase = 0; phase < kPhaseCount; phase++) {
      const Sample & sample = run.phases[phase];
      if (!sample.ran || sample.seconds.empty()) {
        NLogPrint("ndmspc-cli: %-16s %9s     %9s", run.PhaseName(static_cast<Phase>(phase)).c_str(), "(no run)",
                  "(no run)");
        continue;
      }
      // Four decimals: a cache hit is milliseconds on a small tree and microseconds on a big one, so three
      // would print 0.000 and hide the very difference being measured.
      NLogPrint("ndmspc-cli: %-16s %9.4f s %9.4f s", run.PhaseName(static_cast<Phase>(phase)).c_str(),
                sample.Median(), sample.Best());
    }
    const double cold = run.phases[kColdCacheOff].Median();
    const double warm = run.phases[kWarmCacheHit].Median();
    if (cold > 0 && warm > 0) NLogPrint("ndmspc-cli: warm vs cold : %.1fx faster", cold / warm);
  }
}

} // namespace Ndmspc
