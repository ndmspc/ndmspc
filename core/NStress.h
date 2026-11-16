#ifndef Ndmspc_NStress_H
#define Ndmspc_NStress_H

#include <TObject.h>

#include <string>
#include <vector>

namespace Ndmspc {

class NGnTree;

/**
 * @class NStress
 * @brief Builds a big NGnTree out of one source and measures reshape on it, with and without the cache.
 *
 * One source file is copied into a grid of numbered directories, and the existing directory importer turns
 * that layout back into a tree. The directories carry only bin labels — the axis names and their order
 * come from the copy tree this class builds — so the same layout can be imported under any names.
 *
 * The copies are made by `NGnTree::Process` over that copy tree, so `NDMSPC_EXECUTION_MODE` selects how
 * they are spread (ipc, tcp, thread) rather than a hand-rolled loop.
 *
 * ## Measuring
 *
 * `Run()` measures three phases of one combination and **records** them; `GetResults()` returns them as an
 * NGnTree with a `run` and a `phase` axis, so several runs — a local disk and an EOS copy, say — end up in
 * one tree that can be reshaped and drawn like any other.
 *
 * The phases are the honest ones: cache off (the baseline), cache on with a cold build (baseline plus what
 * storing costs), and the same combination again (what a revisit costs). A phase that could not reshape is
 * reported as having no timing at all, because a failed reshape returns instantly and would otherwise read
 * as an enormous speed-up.
 *
 * ## Use from ROOT/Cling
 *
 * @code
 * Ndmspc::NStress s;
 * s.SetWorkdir("/tmp/stress");
 * s.SetAxes("a1=10,a2=20");
 * s.SetSource("data.root");        // omit to generate a synthetic source
 * s.Run("local");
 * s.SetWorkdir("/eos/stress");     // same measurement, different storage
 * s.Run("eos");
 * s.GetResults()->Reshape("default", {{0},{1}});
 * @endcode
 */
class NStress : public TObject {
  public:
  /// The measurement phases, in the order they run.
  enum Phase { kColdCacheOff = 0, kColdCacheOn = 1, kWarmCacheHit = 2, kPhaseCount = 3 };

  /// One phase's measurements: one entry per repeat.
  struct Sample {
    std::vector<double> seconds; ///< One measurement per repeat
    bool                ran{false}; ///< False when the reshape failed, so there is no timing

    /// The middle measurement, which is what a noisy machine is best reported by.
    double Median() const;
    /// The fastest measurement.
    double Best() const;
  };

  /// What one run measured, kept so several runs can be reported together.
  struct RunResult {
    std::string             label;  ///< What the caller called this run
    std::string             source; ///< The source file that was replicated
    std::string             workdir;///< Where the grid was built
    Sample                  phases[kPhaseCount]; ///< The measurements, one per phase

    /// What a phase is called in the table and in the results tree's `phase` axis.
    std::string PhaseName(Phase phase) const;
  };

  NStress();
  ~NStress() override;

  /// @name What to build
  /// @{
  /**
   * @brief Where the grid of copies goes.
   *
   * Moving the workdir forgets a previously resolved source, so the next run generates (or finds) one in
   * the new place rather than silently replicating the old workdir's file.
   *
   * @param workdir The directory; it is created if missing.
   */
  void SetWorkdir(const std::string & workdir)
  {
    if (workdir == fWorkdir) return;
    fWorkdir = workdir;
    // Only a generated source is forgotten; an explicit SetSource stays the caller's.
    if (!fSourceGiven) fSource.clear();
  }
  /// The grid axes: `a1=10,a2=20` (a count, or a comma list of labels). Their order is the axis order.
  void SetAxes(const std::string & axes) { fAxes = axes; }
  /// The file name inside every leaf (default `d.root`).
  void SetBasename(const std::string & basename) { fBasename = basename; }
  /// The source NGnTree to replicate; empty means one is generated.
  void SetSource(const std::string & source)
  {
    fSource      = source;
    fSourceGiven = !source.empty();
  }
  /// Synthetic source: axis values per dimension, and entries per object.
  void SetGenerate(int bins, int entries)
  {
    fBins    = bins;
    fEntries = entries;
  }
  /// Levels to reshape; empty means "every variable axis, split in two layers", read from the tree.
  void SetLevels(const std::string & levels) { fLevels = levels; }
  /// How many times each repeatable phase runs (default 1).
  void SetRepeat(int repeat) { fRepeat = repeat; }
  /// Refuse a layout bigger than this many bytes (default: no limit).
  void SetMaxBytes(long long maxBytes) { fMaxBytes = maxBytes; }
  /// Rebuild the layout even when it already looks right.
  void SetForce(bool force) { fForce = force; }
  /// Check the copies are separate files of identical content (default on).
  void SetVerify(bool verify) { fVerify = verify; }
  /// @}

  /// @name What to do
  /// @{
  /**
   * @brief Build the grid of copies.
   * @return True when the layout was built and is importable.
   */
  bool Build();

  /**
   * @brief Check the layout is worth measuring: separate files, identical content.
   * @return True when it is.
   */
  bool Verify();

  /**
   * @brief Measure the phases of one run and record them.
   *
   * Builds (or reuses) the imported tree, then measures cache off, cache on with a cold build, and the
   * same combination again.
   *
   * @param label What to call this run in the results (e.g. `local`, `eos`). Empty means the workdir.
   * @return True when every phase ran and a revisit was cheaper than a cold build.
   */
  bool Run(const std::string & label = "");

  /**
   * @brief The runs so far as an NGnTree: one point per (run, phase).
   *
   * The tree has a `run` axis (the labels given to `Run`) and a `phase` axis, with a `seconds` parameter
   * per point and a `ratio` (that phase against the run's own cold baseline). Being an ordinary tree, it
   * can be reshaped, drawn and exported like any other.
   *
   * @param outFile Where to write it; empty writes to a temporary file.
   * @return A tree the caller owns, or nullptr when nothing has been measured.
   */
  NGnTree * GetResults(const std::string & outFile = "") const;
  /// @}

  /// @name What was measured
  /// @{
  /**
   * @brief The recorded runs.
   * @return Every run measured so far, in the order they ran.
   */
  const std::vector<RunResult> & GetRuns() const { return fRuns; }
  /// The cold (cache off) median of the last run, in seconds.
  double GetCold() const;
  /// The warm (cache hit) median of the last run, in seconds.
  double GetWarm() const;
  /// How much cheaper a revisit was than a cold build, as a factor.
  double GetSpeedup() const;
  /// The levels the last run measured.
  const std::string & GetResolvedLevels() const { return fResolvedLevels; }
  /// @}

  /// Print the configuration and the table of every recorded run.
  void Print(Option_t * option = "") const override;

  private:
  /**
   * @brief The levels to reshape, defaulting to every variable axis split in two layers.
   * @param tree The imported tree.
   * @return Levels as JSON.
   */
  static std::string DefaultLevels(NGnTree * tree);

  /// The labels of one axis, parsed from `fAxes`.
  struct AxisSpec {
    std::string              name;   ///< The axis name
    std::vector<std::string> labels; ///< One label per bin
  };
  /// Parse `fAxes` into the grid's axes.
  std::vector<AxisSpec> ParseAxes() const;
  /// The source to replicate, generated when `fSource` is empty.
  std::string ResolveSource();

  std::string fWorkdir;         ///< Where the grid is built
  std::string fAxes{"a1=2,a2=2"}; ///< The grid axes, as given
  std::string fBasename{"d.root"}; ///< The file name inside every leaf
  std::string fSource;          ///< The source tree; empty means generate
  std::string fLevels;          ///< Levels to reshape; empty means derive them
  std::string fResolvedLevels;  ///< The levels the last run actually used
  int         fBins{6};         ///< Synthetic source: axis values per dimension
  int         fEntries{20000};  ///< Synthetic source: entries per object
  int         fRepeat{1};       ///< Repeats per repeatable phase
  long long   fMaxBytes{-1};    ///< Refuse a layout bigger than this
  bool        fForce{false};    ///< Rebuild even when the layout looks right
  bool        fVerify{true};    ///< Check the copies
  bool        fSourceGiven{false}; ///< The caller named the source, so a workdir change keeps it

  std::vector<RunResult> fRuns; ///< Every run measured so far

  /// \cond CLASSIMP
  ClassDefOverride(NStress, 1);
  /// \endcond
};

} // namespace Ndmspc
#endif
