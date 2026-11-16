///
/// Reshape caching: build a tree once, store the combinations you care about inside it, and let the next
/// reader load them instead of reshaping.
///
/// A reshape walks every point of the tree, so over a real analysis it takes seconds to minutes — and the
/// result depends only on the file, the binning definition and the levels. That makes it cacheable: the
/// second request for the same combination is a file read.
///
/// This tutorial walks the whole path in one macro:
///
///   1. process a tree            → only `ngnt` and `outputs`, no navigator
///   2. check what is stored      → nothing yet
///   3. reshape + store           → what a `ngnt/reshape` request does
///   4. prove it is a cache hit   → the same combination, measured both ways
///   5. store more combinations   → several coexist, keyed by their levels
///   6. list them                 → what a client is offered on `open`
///   7. time a repeat             → the point of the exercise
///
/// Run it with:
///
///   ndmspc-run -n 4 -v tutorial/09_caching/NReshapeCache01Gaus.C
///
/// or from a ROOT session:
///
///   root -l -b -q tutorial/09_caching/NReshapeCache01Gaus.C
///
#include <cstdio>
#include <string>
#include <vector>

#include <TAxis.h>
#include <TObjArray.h>
#include <TF1.h>
#include <TFitResult.h>
#include <TH1D.h>
#include <TFile.h>
#include <TKey.h>
#include <TRandom3.h>
#include <TSystem.h>

#include <ndmspc/core/NGnNavigator.h>
#include <ndmspc/core/NGnTree.h>
#include <ndmspc/core/NLogger.h>
#include <ndmspc/core/NParameters.h>
#include <ndmspc/core/NReshapeCache.h>
#include <ndmspc/core/NUtils.h>

void NReshapeCache01Gaus(std::string outFile = "NReshapeCache01Gaus.root", bool refresh = false)
{
  gErrorIgnoreLevel = kError;

  // The narrative goes through `NLogInfo`, the project's logging call — so it obeys the console setting, and
  // **run this with `-v`** to see it:
  //
  //   ndmspc-run -n 4 -v tutorial/09_caching/NReshapeCache01Gaus.C
  //
  // Without `-v`, `ndmspc-run` sets NDMSPC_LOG_RUN=1, which *disables* console output (it is meant for
  // progress lines, not narrative), so the steps below are silent. Turning console output on here instead
  // would also unveil every library INFO line, so the caller picks the verbosity.

  // ---------------------------------------------------------------------------------------------
  // Step 1 — a processed tree holds no navigator
  // ---------------------------------------------------------------------------------------------
  // Three axes give 5 x 6 x 5 = 150 points, the same shape the other tutorials use. Processing writes the
  // tree and its outputs; nothing is reshaped, because what a navigator looks like is a choice a reader
  // makes, not something a producer can guess.
  if (refresh || gSystem->AccessPathName(outFile.c_str())) {
    NLogInfo("\n=== 1. process a tree ===");

    TObjArray * axes = new TObjArray();
    axes->Add(Ndmspc::NUtils::CreateAxisFromLabels("mean", "Mean", {"-2.5", "-1.25", "0.0", "1.25", "2.5"}));
    axes->Add(Ndmspc::NUtils::CreateAxisFromLabels("sigma", "Sigma", {"0.5", "1.0", "1.5", "2.0", "2.5", "3.0"}));
    axes->Add(
        Ndmspc::NUtils::CreateAxisFromLabels("entries", "Entries", {"100", "1000", "10000", "100000", "1000000"}));

    Ndmspc::NGnTree * producer = new Ndmspc::NGnTree(axes, outFile);
    producer->GetBinning()->AddBinningDefinition("default", {{"mean", {{1}}}, {"sigma", {{1}}}, {"entries", {{1}}}});
    producer->InitParameters({"meanFit", "sigmaFit"});

    Ndmspc::NGnProcessFuncPtr fill = [](Ndmspc::NBinningPoint * point, TList * /*output*/, TList * outputPoint,
                                        int /*threadId*/) {
      const double mean   = point->GetBinCenter("mean");
      const double sigma  = point->GetBinCenter("sigma");
      const int    nBins  = 200;

      // The `entries` axis is a label, not a decoration: it says how many entries this point's histogram
      // holds, so the axis actually drives the work instead of being carried along unused.
      const int nEntries = std::stoi(point->GetBinLabel("entries"));

      TH1D * histogram = new TH1D("hGaus", "g", nBins, -10, 10);
      thread_local TRandom3 random(0);
      for (int i = 0; i < nEntries; i++) histogram->Fill(random.Gaus(mean, sigma));

      // A function on ROOT's global list is what makes multi-worker runs crash: it must be detached.
      TF1 * gaus = new TF1("gausFunc", "gaus", -10, 10);
      gaus->AddToGlobalList(false);
      TFitResultPtr fit = histogram->Fit(gaus, "QS");
      if (point->GetParameters() != nullptr) {
        point->GetParameters()->SetParameter("meanFit", fit->Parameter(1), fit->Error(1));
        point->GetParameters()->SetParameter("sigmaFit", fit->Parameter(2), fit->Error(2));
      }
      outputPoint->Add(histogram);
    };

    producer->Process(fill, json::object(), "default");
    delete producer;
  }
  else {
    NLogInfo("\n=== 1. process a tree (reusing '%s') ===", outFile.c_str());
  }

  Ndmspc::NGnTree * tree = Ndmspc::NGnTree::Open(outFile);
  if (tree == nullptr) {
    NLogInfo("cannot open %s", outFile.c_str());
    return;
  }
  NLogInfo("  points=%lld, axes=%zu", (long long)tree->GetEntries(), tree->GetBinning()->GetAxes().size());
  NLogInfo("  combinations stored in the file: %d", Ndmspc::NReshapeCache::CountStored(outFile));

  const std::vector<std::vector<int>> twoLayers = {{0, 2}, {1}};
  const std::vector<std::vector<int>> oneLayer  = {{0, 1, 2}};

  // ---------------------------------------------------------------------------------------------
  // Step 2 — the cache key, before anything is built
  // ---------------------------------------------------------------------------------------------
  // Two combinations have two different keys, which is why they can coexist: the key is everything the
  // navigator is a function of, and the levels are part of it. On a fresh file neither is stored; on a
  // second run both are, and that difference is what the cache is for.
  NLogInfo("\n=== 2. the key is the combination ===");
  {
    Ndmspc::NGnNavigator * existing = Ndmspc::NReshapeCache::Load(Ndmspc::NReshapeCache::KeyFor(tree, twoLayers), tree);
    const bool             stored   = existing != nullptr;
    delete existing;
    NLogInfo("  [[0,2],[1]] -> %s  (%s)", Ndmspc::NReshapeCache::Digest(Ndmspc::NReshapeCache::KeyFor(tree, twoLayers)).c_str(),
           stored ? "already stored" : "not built yet");
  }
  NLogInfo("  [[0,1,2]]   -> %s  (%s)", Ndmspc::NReshapeCache::Digest(Ndmspc::NReshapeCache::KeyFor(tree, oneLayer)).c_str(),
         Ndmspc::NReshapeCache::Load(Ndmspc::NReshapeCache::KeyFor(tree, oneLayer), tree) != nullptr ? "already stored" : "not built yet");
  // A third combination nobody asked for: its key exists, but no entry does, so it would reshape.
  NLogInfo("  [[0],[1,2]] -> %s  (%s)", Ndmspc::NReshapeCache::Digest(Ndmspc::NReshapeCache::KeyFor(tree, {{0}, {1, 2}})).c_str(),
         Ndmspc::NReshapeCache::Load(Ndmspc::NReshapeCache::KeyFor(tree, {{0}, {1, 2}}), tree) != nullptr ? "already stored" : "not built — it would reshape");

  // ---------------------------------------------------------------------------------------------
  // Step 3 — reshape once, store the result, time it
  // ---------------------------------------------------------------------------------------------
  // This is exactly what the `ngnt/reshape` handler does: look for the combination, and build and store it
  // only on a miss. A store that fails leaves the reshape intact — the cache is never allowed to break a
  // request it is serving.
  NLogInfo("\n=== 3. reshape and store [[0,2],[1]] ===");
  double coldSeconds = 0;
  {
    // One call: loads the combination when it is stored, otherwise reshapes and stores it. This is the whole
    // policy, and it is the cache's rather than each caller's.
    bool                   rebuilt = false;
    const double           started = Ndmspc::NUtils::Now();
    Ndmspc::NGnNavigator * nav =
        Ndmspc::NReshapeCache::LoadOrReshape(tree, twoLayers, &rebuilt);
    const double elapsed = Ndmspc::NUtils::Since(started);

    if (nav == nullptr) {
      NLogInfo("  reshape failed");
      delete tree;
      return;
    }
    if (rebuilt) {
      coldSeconds = elapsed;
      NLogInfo("  built in %.4f s, children=%zu", elapsed, nav->GetChildren().size());
    }
    else {
      NLogInfo("  already stored, loaded in %.4f s, children=%zu", elapsed, nav->GetChildren().size());
    }
    delete nav;
  }
  NLogInfo("  combinations stored in the file: %d", Ndmspc::NReshapeCache::CountStored(outFile));

  // ---------------------------------------------------------------------------------------------
  // Step 4 — the same combination again is a file read
  // ---------------------------------------------------------------------------------------------
  NLogInfo("\n=== 4. the same combination, now stored ===");
  {
    const double           started = Ndmspc::NUtils::Now();
    Ndmspc::NGnNavigator * nav     = Ndmspc::NReshapeCache::Load(Ndmspc::NReshapeCache::KeyFor(tree, twoLayers), tree);
    const double           elapsed = Ndmspc::NUtils::Since(started);
    if (nav == nullptr) {
      NLogInfo("  MISS — the entry was not found, which should not happen after step 3");
    }
    else {
      NLogInfo("  loaded in %.4f s, children=%zu", elapsed, nav->GetChildren().size());
      if (coldSeconds > 0) {
        NLogInfo("  a cold reshape took %.4f s — this is %.1fx faster", coldSeconds,
               elapsed > 0 ? coldSeconds / elapsed : 0.0);
      }
      delete nav;
    }
  }

  // ---------------------------------------------------------------------------------------------
  // Step 5 — a second combination lives beside the first
  // ---------------------------------------------------------------------------------------------
  // Nothing has to be chosen ahead of time: whatever is reshaped and stored is kept, and each combination
  // has its own entry. A reshape whose levels miss an axis fails, and a failed one is never stored.
  NLogInfo("\n=== 5. store a second combination [[0,1,2]] ===");
  {
    // The same one call as step 3, with different levels: the two combinations are independent entries, and
    // a second run must not pay for either reshape again.
    bool                   rebuilt = false;
    Ndmspc::NGnNavigator * nav     = Ndmspc::NReshapeCache::LoadOrReshape(tree, oneLayer, &rebuilt);
    if (nav == nullptr) {
      NLogInfo("  reshape failed (a single layer must still cover every axis)");
    }
    else {
      NLogInfo("  %s, children=%zu, levels=%zu", rebuilt ? "built and stored" : "already stored, loaded",
               nav->GetChildren().size(), nav->GetNLevels());
      delete nav;
    }
  }
  NLogInfo("  combinations stored in the file: %d", Ndmspc::NReshapeCache::CountStored(outFile));

  // ---------------------------------------------------------------------------------------------
  // Step 6 — what the file offers a reader
  // ---------------------------------------------------------------------------------------------
  // `List` reads each entry's metadata only, never the navigators, so it is cheap enough to answer on
  // `open`. This is what a client is shown before it decides which combination to look at.
  NLogInfo("\n=== 6. what the file offers ===");
  NLogInfo("  %s", Ndmspc::NReshapeCache::List(outFile).c_str());

  // ---------------------------------------------------------------------------------------------
  // Step 7 — a repeat visit costs a read, not a reshape
  // ---------------------------------------------------------------------------------------------
  // The two stored combinations are independent: loading one after the other costs two reads, and neither
  // touches the tree's points.
  NLogInfo("\n=== 7. revisiting both ===");
  for (const auto & levels : {twoLayers, oneLayer}) {
    const double           started = Ndmspc::NUtils::Now();
    Ndmspc::NGnNavigator * nav     = Ndmspc::NReshapeCache::Load(Ndmspc::NReshapeCache::KeyFor(tree, levels), tree);
    const double           elapsed = Ndmspc::NUtils::Since(started);
    NLogInfo("  %-14s %s in %.4f s", json(levels).dump().c_str(), nav ? "HIT " : "MISS", elapsed);
    delete nav;
  }

  // ---------------------------------------------------------------------------------------------
  // Step 8 — read it? keep it? the four modes
  // ---------------------------------------------------------------------------------------------
  // The cache answers two questions, and each can be answered independently:
  //   Use          read it; a miss is reshaped and NOT kept
  //   UseOrStore   read it; a miss is reshaped and kept (the default — the cache fills itself)
  //   Rebuild      do not read it; reshape and keep, over the old entry
  //   Ignore       do not read it; reshape and keep nothing (iterating on a macro)
  // Written out here so the difference is visible rather than inferred from a log line.
  NLogInfo("\n=== 8. the four cache modes on one combination ===");
  {
    // A combination nothing has asked for yet, so every mode starts from the same place: a miss.
    const std::vector<std::vector<int>> fresh = {{0, 1}, {2}};
    using Mode = Ndmspc::NReshapeCache::CacheMode;
    // Named explicitly rather than inferred: `make_pair` would widen the mode to its underlying integer.
    const struct {
      const char * name;
      Mode         mode;
    } modes[] = {{"Use", Mode::kUse},
                 {"UseOrStore", Mode::kUseOrStore},
                 {"Rebuild", Mode::kRebuild},
                 {"Ignore", Mode::kIgnore}};

    for (const auto & one : modes) {
      // Start from nothing, so each mode is judged on its own rather than on what the one before it left.
      Ndmspc::NReshapeCache::Key key = Ndmspc::NReshapeCache::KeyFor(tree, fresh);
      Ndmspc::NReshapeCache::Remove(key);

      bool                   rebuilt = false;
      Ndmspc::NGnNavigator * nav     = Ndmspc::NReshapeCache::LoadOrReshape(tree, fresh, &rebuilt, one.mode);
      // Whether this combination is now stored: the file may hold others, so ask about the entry itself.
      Ndmspc::NGnNavigator * check = Ndmspc::NReshapeCache::Load(key, tree);
      NLogInfo("  %-11s %s, kept: %s", one.name, rebuilt ? "reshaped" : "loaded", check ? "yes" : "no");
      delete check;
      delete nav;
    }
    NLogInfo("  Use and Ignore both leave nothing behind; UseOrStore and Rebuild both keep it.");
  }

  NLogInfo("\n=== summary ===");
  NLogInfo("  combinations in '%s': %d", outFile.c_str(), Ndmspc::NReshapeCache::CountStored(outFile));
  NLogInfo("  a reader that asks for one of them gets it without reshaping.");
  NLogInfo("  re-run with refresh=true (or --macro-params '\"%s\",true') for a cold measurement.",
         outFile.c_str());

  tree->Close();
  delete tree;
}
