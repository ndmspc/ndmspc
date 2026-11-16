#ifndef Ndmspc_NReshapeCache_H
#define Ndmspc_NReshapeCache_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "NGnNavigator.h"

namespace Ndmspc {

class NGnTree;

/**
 * @class NReshapeCache
 * @brief Keeps reshaped navigators (and their map JSON) so a combination is not built twice.
 *
 * A reshape reads the tree entry by entry and can take minutes, yet its result depends only on the
 * inputs a Key holds. Two requests that agree on those produce the same navigator, so the second one
 * loads it. A view switch then costs one file read instead of a reshape.
 *
 * ## Where an entry lives
 *
 * **In the tree file first.** An entry is written into the source file itself, under
 * `reshape/<digest>/`, so a fully precached file travels with its data and is fast for anyone who opens
 * it. This is the preferred home.
 *
 * **The server cache second.** When the source cannot be written — EOS, CVMFS, a read-only mount, a
 * file another writer holds — the same entry goes to `NDMSPC_CACHE_DIR` instead (default
 * `~/.cache/ndmspc/reshape`). A read that finds nothing in the file falls back to it, so a read-only
 * source still avoids reshaping twice.
 *
 * ## What identifies a combination
 *
 * The source tree's **content fingerprint** — its entry count and total byte count — plus the binning
 * definition, the levels and the averaging flag. Deliberately *not* the file's size and modification
 * time: writing a navigator into the file changes both, so a timestamped key would invalidate itself
 * the moment it was stored. The entry and byte counts describe the tree, so adding a navigator leaves
 * them alone — an entry stays valid across its own write — while a source that genuinely gains or loses
 * data still misses.
 *
 * ## Reading and writing
 *
 * Reads are lock-free: a reader opens the tree `READ` and never takes a lock, so any number of readers may
 * run alongside a writer. **Writers are serialised** by an advisory lock on a `<file>.lock` sidecar, because
 * ROOT's `UPDATE` open is a read-modify-write of the whole file — without the lock, two processes precaching
 * one tree each dropped the other's entry, silently and with a zero exit status.
 *
 * Every operation is best-effort: a cache that cannot be read or written logs and reports a miss, and
 * never fails the reshape it is serving. `NDMSPC_RESHAPE_CACHE=0` turns the whole thing off.
 */
class NReshapeCache {
  public:
  /**
   * @brief The in-file directory an entry lives in, inside the tree file.
   * @param digest The combination's digest.
   * @return e.g. `reshape/27be9ca2a6b58e93`.
   */
  static std::string EntryDirectory(const std::string & digest);

  /**
   * @brief The inputs a reshaped navigator is a function of.
   *
   * Two requests with equal keys want the same navigator; any difference means a different one. The
   * fingerprint fields are the source tree's, not the file's, so storing an entry does not change them.
   */
  struct Key {
    std::string                   file;        ///< Absolute path of the source file
    long long                     nEntries{0}; ///< The tree's entry count — part of the fingerprint
    long long                     totBytes{0}; ///< The tree's byte count — part of the fingerprint
    std::string                   binning;     ///< Binning definition name ("" = the current one)
    std::vector<std::vector<int>> levels;      ///< Levels, as passed to Reshape
    bool                          averages{true}; ///< Whether deeper parameters are averaged

    /**
     * @brief Where the entry is read and written — **not** part of the identity.
     *
     * `file` says what the navigator was built from; this says where it is kept, and the two are separate
     * on purpose: the same combination is the same navigator wherever it lives, so an entry can be moved
     * between homes and still be found by the same digest.
     *
     * - `""` — the cache directory (`CacheFilePath(file)`), which leaves the input untouched.
     * - `file` itself — in-file, the input gains `reshape/<digest>/`.
     */
    std::string cacheFile;

    /**
     * @brief Fill the file identity from a tree: its path and content fingerprint.
     * @param tree The tree a navigator is reshaped from.
     * @return This key, for chaining.
     */
    Key & FromTree(NGnTree * tree);

    /**
     * @brief Fill the file identity from a path, opening it header-only to fingerprint it.
     *
     * For a prune, which asks "what does this file look like now?" with no tree open. Nothing but the
     * tree header is read. The fingerprint stays {0,0} when the file cannot be read, which callers
     * treat as "cannot tell" rather than as a real identity.
     *
     * @param path The source file.
     * @return This key, for chaining.
     */
    Key & FromPath(const std::string & path);
  };

  /**
   * @brief Whether a path names remote storage (`root://`, `alien://`, `http(s)://`).
   *
   * The distinction decides the default home: writing an entry into a local file is cheap and keeps the
   * cache with the data, while writing into remote storage is an `UPDATE` round-trip to a shared
   * filesystem the caller may not even be allowed to write.
   *
   * @param path A file path or URL.
   * @return True when the path carries a scheme other than `file://`.
   */
  static bool IsRemote(const std::string & path);

  /**
   * @brief The home an entry is kept in when the caller states no preference.
   *
   * - A **local** file returns itself: the entry goes in-file, beside the data it describes.
   * - A **remote** file returns `""`, meaning the cache directory.
   *
   * @param file The source file (or URL).
   * @return `file` for a local input, `""` for a remote one.
   */
  static std::string DefaultCacheFile(const std::string & file);

  /**
   * @brief The cache-directory file that mirrors one input's path.
   *
   * `<NDMSPC_CACHE_DIR>/<host>/<full path>` for a URL, `<NDMSPC_CACHE_DIR>/<full path>` for a local file,
   * so one input always maps to one cache file — which the flat digest-named scheme could not express, and
   * the reason a listing could not answer per input. The mirror keeps the host *and* the directory path, so
   * two files with the same basename in different places never collide.
   *
   * The file holds the same `reshape/<digest>/` layout as an in-file cache, so `List`, `Load`, `Store` and
   * `Prune` treat it exactly like one.
   *
   * @param file The source file (or URL).
   * @return The mirrored path, or "" when the cache directory is unavailable.
   */
  static std::string CacheFilePath(const std::string & file);

  /**
   * @brief The cache entry name for a combination: a stable hex digest of the key.
   * @param key The combination.
   * @return Lowercase hex digest, safe as a directory name.
   */
  static std::string Digest(const Key & key);

  /**
   * @brief The server-side fallback directory, created if missing.
   * @return The directory, or "" when it cannot be created.
   */
  static std::string Directory();

  /**
   * @brief Whether the cache is enabled (`NDMSPC_RESHAPE_CACHE`, on by default).
   * @return True when reads and writes may happen.
   */
  static bool Enabled();

  /**
   * @brief The server-side fallback file for a combination.
   * @param key The combination.
   * @return Its path inside Directory(), or "" when unavailable.
   */
  static std::string Path(const Key & key);

  /**
   * @brief Load the navigator stored for a combination.
   *
   * Looks in the tree file first, then in the server fallback. Returns nullptr on a miss, when the
   * cache is off, or when the entry was written by another cache format — in which case the unusable
   * entry is dropped, so the next reshape rebuilds it.
   *
   * @param key The combination.
   * @param tree The tree to attach to the loaded navigator (its children too). The caller keeps
   *        ownership of the tree; the navigator only points at it. A navigator's tree is transient, so
   *        this is what makes a loaded one usable.
   * @return A navigator the caller owns, or nullptr when there is nothing usable to load.
   */
  static NGnNavigator * Load(const Key & key, NGnTree * tree);

  /**
   * @brief Store a navigator for a combination, replacing any entry already there.
   *
   * Writes into the tree file when it can be opened UPDATE, otherwise into the server fallback.
   *
   * @param key The combination the navigator was built for.
   * @param nav The navigator to store; ownership stays with the caller.
   * @return True when the entry was written somewhere.
   */
  static bool Store(const Key & key, NGnNavigator * nav);

  /**
   * @brief Load the map JSON exported for a combination, if one was kept.
   *
   * Exporting the whole navigator to JSON is what a `map` request spends its time on, so the export is
   * kept beside the navigator rather than built again.
   *
   * @param key The combination.
   * @param out Filled with the JSON text on a hit; untouched otherwise.
   * @return True when a stored export was found and read.
   */
  static bool LoadMapJson(const Key & key, std::string & out);

  /**
   * @brief Keep the map JSON exported for a combination.
   * @param key The combination.
   * @param text The JSON to keep.
   * @return True when it was written.
   */
  static bool StoreMapJson(const Key & key, const std::string & text);

  /**
   * @brief Remove the entry for a combination, if there is one.
   *
   * Removes it from wherever it lives. An entry inside a read-only source is left alone, since it
   * cannot be reached for writing either.
   *
   * @param key The combination.
   * @return True when something was removed.
   */
  static bool Remove(const Key & key);

  /**
   * @brief The cache key of one reshape combination of a tree.
   *
   * The key is what a navigator is a function of: the file and its content fingerprint, the binning
   * definition and the levels. Two calls with the same tree and levels give the same digest, which is what
   * lets a stored navigator be found again. It lives here because the key is the cache's own vocabulary —
   * every caller would otherwise repeat these three lines, and could get the binning name wrong.
   *
   * @param tree An open tree.
   * @param levels The combination, as passed to `NGnTree::Reshape`.
   * @return The key, ready for `Load` / `Store`.
   */
  static Key KeyFor(NGnTree * tree, const std::vector<std::vector<int>> & levels);

  /**
   * @brief What a reshape does with the cache.
   *
   * Four answers, because there are exactly two questions — *may I read the cache?* and *may I write it?* —
   * and each mode answers both. Naming them by their behaviour rather than by a flag is what makes
   * "recompute it, but do not keep it" sayable at all.
   */
  enum class CacheMode {
    kUse,       ///< Read it; on a miss reshape and **keep nothing**
    kUseOrStore,///< Read it; on a miss reshape and store (the default — the cache fills itself)
    kRebuild,   ///< Do not read it; reshape and store over what was there
    kIgnore,    ///< Do not read it; reshape and keep nothing — for iterating on a macro
  };

  /**
   * @brief Whether a mode reads the cache.
   * @param mode The mode.
   * @return True for `Use` and `UseOrStore`.
   */
  static bool Reads(const CacheMode mode)
  {
    return mode == CacheMode::kUse || mode == CacheMode::kUseOrStore;
  }

  /**
   * @brief Whether a mode writes the cache.
   * @param mode The mode.
   * @return True for `UseOrStore` and `Rebuild`.
   */
  static bool Writes(const CacheMode mode)
  {
    return mode == CacheMode::kUseOrStore || mode == CacheMode::kRebuild;
  }

  /**
   * @brief Parse a mode name.
   * @param name One of `use`, `use-or-store`, `rebuild`, `ignore`. `""` means the default, and `refresh`
   *             (the old flag) and `skip` (an older word for `ignore`) are still accepted.
   * @param mode Filled with the mode.
   * @return False when the name is not one of them.
   */
  static bool ParseMode(const std::string & name, CacheMode & mode);

  /**
   * @brief Load a combination, building and storing it only when it is not there yet.
   *
   * The shape every caller wants — the reshape handler, the stress tool and a precache all do exactly this
   * — so it is one call rather than five lines repeated. A reshape that fails returns nullptr and stores
   * nothing, so a broken combination is never left in the file.
   *
   * @param tree An open tree.
   * @param levels The combination.
   * @param rebuilt Set to true when the navigator was built rather than loaded; nullptr is allowed.
   * @param mode What to do with the cache (default `kUseOrStore`: read it, and fill it on a miss).
   * @param cacheFile Where to keep the entry. Absent means `DefaultCacheFile(tree's file)`; an explicit `""`
   *                  means the cache directory. The two are different, which is why this is an optional
   *                  rather than a string whose empty value would have to mean both.
   * @return A navigator the caller owns, or nullptr when the reshape failed.
   */
  static NGnNavigator * LoadOrReshape(NGnTree * tree, const std::vector<std::vector<int>> & levels,
                                      bool * rebuilt = nullptr, CacheMode mode = CacheMode::kUseOrStore,
                                      const std::optional<std::string> & cacheFile = std::nullopt);

  /**
   * @brief How many reshape combinations a file holds.
   *
   * Counts the entry directories under `reshape/`, reading only the directory structure — no navigator is
   * deserialized, so it is cheap enough to report alongside a measurement.
   *
   * @param file The NGnTree file.
   * @return The number of stored combinations; 0 when there are none or the file cannot be read.
   */
  static int CountStored(const std::string & file);

  /**
   * @brief The metadata an entry records, for diagnostics and for refusing stale entries.
   * @param key The combination.
   * @param nav The navigator being stored.
   * @return A JSON object describing the entry.
   */
  static std::string BuildMeta(const Key & key, NGnNavigator * nav);

  /**
   * @brief The combinations already stored for one source file.
   *
   * A combination that is stored is one that can be switched to without reshaping, so this tells a
   * client which views it can offer cheaply. Both homes are read: the tree file's own entries and the
   * server fallback's. Entries for other files, and entries this build cannot read, are left out.
   *
   * @param file The source file's absolute path; "" lists every server-side combination.
   * @return A JSON array of objects: digest, file, binning, levels, averages, nChildren, nLevels,
   *         bytes, created, and `home` (`in-file` or `cache`).
   */
  static std::string List(const std::string & file);

  /// What a prune did, so a caller can report it rather than only logging it.
  struct PruneStats {
    size_t    removed{0};    ///< Entries deleted
    size_t    kept{0};       ///< Entries left alone
    long long bytesFreed{0}; ///< Bytes released — only entries that were their own file count
  };

  /**
   * @brief Delete entries that can no longer be reached.
   *
   * An entry is named by a digest of its inputs, and the file's content fingerprint is one of them. A
   * source that gains or loses data therefore gives every combination of it a **new** digest: the old
   * entries stay where they are but no request can ever name them again. Left alone they accumulate,
   * one set per rewrite.
   *
   * So: keep only the entry matching the file as it is now, and delete the superseded ones. Nothing is
   * deleted on a guess — a group whose source file cannot be read is left untouched, since its
   * staleness cannot be shown. Entries written by another cache format are dropped too, because this
   * build will never load them.
   *
   * @param file Restrict to one source file; "" prunes every file in the server fallback.
   * @return How much was removed and what remains.
   */
  static PruneStats Prune(const std::string & file = "");
};

} // namespace Ndmspc
#endif
