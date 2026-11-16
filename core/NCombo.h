#ifndef Ndmspc_NCombo_H
#define Ndmspc_NCombo_H

#include <string>
#include <vector>

namespace Ndmspc {

class NGnNavigator;
class NGnTree;

/**
 * @brief Reshape chosen combinations of one tree and store them inside it.
 *
 * A processed NGnTree holds only the tree and its outputs: no navigator, no map. This adds the combinations
 * a caller asks for, and nothing else — each one is stored where `NReshapeCache` keeps entries, which is the
 * tree file itself, so the file becomes a precached one that any later reader (a room, a UI, a macro) can
 * load instead of reshaping.
 *
 * The combinations are **not** enumerated from the tree: a levels list is a choice a person or a script
 * makes, and only the ones asked for are built and kept.
 *
 * Optionally the map JSON is stored beside each navigator too. That is what a `map` request otherwise spends
 * its time exporting, so storing it makes the first map of a stored combination a file read.
 */
class NCombo {
  public:
  /// What one combination did.
  struct Result {
    std::vector<std::vector<int>> levels; ///< The combination asked for
    std::string                   digest; ///< Its cache entry name
    bool                          stored{false}; ///< The navigator was written
    bool                          already{false};  ///< It was already there, so it was left alone
    bool                          mapStored{false}; ///< The map JSON was written too
    size_t                        children{0};      ///< Bins at the first layer
    size_t                        nLevels{0};       ///< Layers in the navigator
  };

  /**
   * @brief Reshape and store each combination of one tree.
   *
   * @param tree An open tree; the entries go into the file it came from.
   * @param levels The combinations to store.
   * @param binning The binning definition to reshape; empty means the tree's current one.
   * @param withMap Also store each combination's exported JSON.
   * @param refresh Rebuild a combination that is already stored.
   * @param results Filled with one entry per combination asked for.
   * @return True when every combination ended up stored.
   */
  static bool Store(NGnTree * tree, const std::vector<std::vector<std::vector<int>>> & levels,
                    const std::string & binning, bool withMap, bool refresh, std::vector<Result> & results);

  /**
   * @brief The combinations already stored in a file.
   * @param file The NGnTree file.
   * @return The same JSON array `NReshapeCache::List` answers.
   */
  static std::string List(const std::string & file);

  /**
   * @brief Parse a `--levels` value.
   *
   * Accepts the JSON form (`[[0,2],[1]]`) and a plain one (`0,2:1`), the same `:`-between-layers,
   * `,`-between-axes spelling `stress --axes` uses, since both describe a grid.
   *
   * @param text The value.
   * @param levels Filled with the parsed layers.
   * @return False when the text is not a usable set of layers.
   */
  static bool ParseLevels(const std::string & text, std::vector<std::vector<int>> & levels);

  /**
   * @brief The levels to use when the caller names none: every axis, split in two layers.
   * @param tree The tree.
   * @return The levels, as JSON.
   */
  static std::string DefaultLevels(NGnTree * tree);
};

} // namespace Ndmspc
#endif
