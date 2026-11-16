#include <gtest/gtest.h>

#include <TFile.h>
#include <TNamed.h>
#include <TSystem.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
using nlohmann::json;

#include "05_single_bining/NSingleBinning01Gaus.C"
#include "ndmspc/core/NReshapeCache.h"
#include "ndmspc/core/NGnNavigator.h"
#include "ndmspc/core/NGnTree.h"

namespace {

std::string ReadFile(const std::string & name)
{
  std::ifstream     f(name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

/** Sets an environment variable for as long as the object lives, and puts the old one back after. */
class ScopedEnvVar {
  public:
  ScopedEnvVar(const std::string & name, const std::string & value) : fName(name)
  {
    const char * previous = std::getenv(name.c_str());
    if (previous) {
      fHadPrevious = true;
      fPrevious    = previous;
    }
    setenv(name.c_str(), value.c_str(), 1);
  }
  ~ScopedEnvVar()
  {
    if (fHadPrevious) setenv(fName.c_str(), fPrevious.c_str(), 1);
    else unsetenv(fName.c_str());
  }

  private:
  std::string fName;
  std::string fPrevious;
  bool        fHadPrevious{false};
};

/**
 * The reshape cache exists so a combination is built once: the key names what the navigator depends
 * on, and a stored navigator must come back usable — not merely readable.
 */
class NReshapeCacheTest : public ::testing::Test {
  protected:
  static const std::string testFile;
  static const std::string cacheDir;

  static void SetUpTestSuite()
  {
    std::ifstream f(testFile);
    if (!f.good()) NSingleBinning01Gaus(testFile);
  }
  static void TearDownTestSuite()
  {
    std::remove(testFile.c_str());
    gSystem->Exec(TString::Format("rm -rf %s", cacheDir.c_str()));
  }

  /// A key for the test file with the given levels, kept where the default for a local file says.
  static Ndmspc::NReshapeCache::Key KeyFor(Ndmspc::NGnTree * tree, std::vector<std::vector<int>> levels)
  {
    Ndmspc::NReshapeCache::Key key;
    key.FromTree(tree);
    key.binning   = "";
    key.levels    = std::move(levels);
    key.averages  = true;
    key.cacheFile = Ndmspc::NReshapeCache::DefaultCacheFile(key.file);
    return key;
  }
};

const std::string NReshapeCacheTest::testFile = "test_NReshapeCache.root";
const std::string NReshapeCacheTest::cacheDir = "test_reshape_cache_dir";

} // namespace

/** The same combination always hashes the same; any input that changes the navigator does not. */
TEST_F(NReshapeCacheTest, DigestIsStableAndSensitive)
{
  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  auto base = KeyFor(ngnt, {{0}, {1, 2}});
  const std::string digest = Ndmspc::NReshapeCache::Digest(base);

  EXPECT_EQ(Ndmspc::NReshapeCache::Digest(base), digest) << "the same key must hash the same";
  EXPECT_FALSE(digest.empty());

  auto other = base;
  other.levels = {{0, 1}, {2}};
  EXPECT_NE(Ndmspc::NReshapeCache::Digest(other), digest) << "different levels must not share an entry";

  other        = base;
  other.binning = "b0";
  EXPECT_NE(Ndmspc::NReshapeCache::Digest(other), digest) << "different binning must not share an entry";

  other           = base;
  other.averages  = false;
  EXPECT_NE(Ndmspc::NReshapeCache::Digest(other), digest) << "averages must be part of the key";

  // The content fingerprint, not the file's size and mtime: a source that gains data must miss...
  other          = base;
  other.nEntries = base.nEntries + 1;
  EXPECT_NE(Ndmspc::NReshapeCache::Digest(other), digest) << "more entries in the source must miss";

  other         = base;
  other.totBytes = base.totBytes + 1024;
  EXPECT_NE(Ndmspc::NReshapeCache::Digest(other), digest) << "a source with different content must miss";

  ngnt->Close();
  delete ngnt;
}

/**
 * The reason the key is a content fingerprint: storing an entry writes into the tree file, which
 * changes the file's size and modification time. A key built from those would invalidate its own entry
 * as it was written, so every read would miss and the cache would never hit.
 */
TEST_F(NReshapeCacheTest, WritingAnEntryDoesNotChangeTheKey)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string writable = "test_NReshapeCache_infile.root";
  NSingleBinning01Gaus(writable);

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(writable);
  ASSERT_TRUE(ngnt);
  auto key = KeyFor(ngnt, {{0}, {1, 2}});
  const std::string before = Ndmspc::NReshapeCache::Digest(key);

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(key, nav)) << "the store did not succeed anywhere";

  // Recompute the key from the tree as it is now: it must be the same, or the entry just written is
  // already unreachable.
  auto again = KeyFor(ngnt, {{0}, {1, 2}});
  EXPECT_EQ(Ndmspc::NReshapeCache::Digest(again), before)
      << "writing an entry changed the fingerprint, so the entry invalidated itself";

  Ndmspc::NGnNavigator * loaded = Ndmspc::NReshapeCache::Load(again, ngnt);
  ASSERT_NE(loaded, nullptr) << "the entry written was not found under the same key";

  delete loaded;
  delete nav;
  ngnt->Close();
  delete ngnt;
  std::remove(writable.c_str());
}

/** An entry is kept inside the tree file when it can be written, so the file is precached. */
TEST_F(NReshapeCacheTest, EntryIsStoredInsideTheTreeFile)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string writable = "test_NReshapeCache_precached.root";
  NSingleBinning01Gaus(writable);

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(writable);
  ASSERT_TRUE(ngnt);
  auto key = KeyFor(ngnt, {{0}, {1, 2}});

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(key, nav));

  // It must be in the tree file itself: that is the home that travels with the data.
  {
    TFile * in = TFile::Open(writable.c_str(), "READ");
    ASSERT_TRUE(in);
    const std::string entry = Ndmspc::NReshapeCache::EntryDirectory(Ndmspc::NReshapeCache::Digest(key));
    EXPECT_NE(in->Get(entry.c_str()), nullptr) << "the entry is not inside the tree file";
    in->Close();
    delete in;
  }

  // And the file must still be a working tree: writing an entry must not damage the data.
  ngnt->Close();
  delete ngnt;
  Ndmspc::NGnTree * reopened = Ndmspc::NGnTree::Open(writable);
  ASSERT_TRUE(reopened);
  EXPECT_FALSE(reopened->IsZombie());
  EXPECT_GT(reopened->GetEntries(), 0) << "the tree no longer reads after its entry was written";

  const auto listed = json::parse(Ndmspc::NReshapeCache::List(key.file));
  ASSERT_EQ(listed.size(), 1u) << listed.dump();
  EXPECT_EQ(listed[0]["home"], "in-file");

  reopened->Close();
  delete reopened;
  delete nav;
  std::remove(writable.c_str());
}

/**
 * A source that cannot be written falls back to the server cache.
 *
 * "Cannot be written" is made with `chmod 0444`, which a normal user is stopped by but **root is not**:
 * root holds CAP_DAC_OVERRIDE, so it writes a read-only file happily and the source is then stored in-file
 * rather than in the fallback. GitLab's build containers run as root, so this test asserts the fallback only
 * when the read-only bit actually binds; under root it checks the branch that root really takes.
 */
TEST_F(NReshapeCacheTest, ReadOnlySourceFallsBackToTheCacheDirectory)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string readOnly = "test_NReshapeCache_readonly.root";
  NSingleBinning01Gaus(readOnly);
  // Read-only, the way an EOS/CVMFS source reaches a room.
  ASSERT_EQ(gSystem->Chmod(readOnly.c_str(), 0444), 0);

  // Does the mode actually bind for this process? Root can write a 0444 file, so the premise has to be
  // checked rather than assumed.
  bool modeBinds = true;
  {
    std::ofstream probe(readOnly.c_str(), std::ios::app);
    modeBinds = !probe.good();
  }

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(readOnly);
  ASSERT_TRUE(ngnt);
  auto key = KeyFor(ngnt, {{0}, {1, 2}});

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);
  EXPECT_TRUE(Ndmspc::NReshapeCache::Store(key, nav)) << "the entry must be stored somewhere";

  const std::string entry = Ndmspc::NReshapeCache::EntryDirectory(Ndmspc::NReshapeCache::Digest(key));
  bool              inFile = false;
  {
    TFile * in = TFile::Open(readOnly.c_str(), "READ");
    ASSERT_TRUE(in);
    inFile = in->Get(entry.c_str()) != nullptr;
    in->Close();
    delete in;
  }

  const auto listed = json::parse(Ndmspc::NReshapeCache::List(key.file));
  ASSERT_FALSE(listed.empty()) << "the stored entry was not listed";

  if (modeBinds) {
    // The read-only bit holds: the source is untouched and the entry is in the cache file.
    EXPECT_FALSE(inFile) << "the read-only source was written to!";
    EXPECT_EQ(listed[0]["home"], "cache");
    EXPECT_FALSE(gSystem->AccessPathName(Ndmspc::NReshapeCache::CacheFilePath(key.file).c_str()))
        << "the entry fell back to the cache file";
  }
  else {
    // Running as root: the source is writable, so the entry correctly goes into the tree file.
    EXPECT_TRUE(inFile) << "root can write a 0444 file, so the entry should have gone in-file";
    EXPECT_EQ(listed[0]["home"], "in-file");
  }

  // Either way it must load back.
  Ndmspc::NGnNavigator * loaded = Ndmspc::NReshapeCache::Load(KeyFor(ngnt, {{0}, {1, 2}}), ngnt);
  EXPECT_NE(loaded, nullptr) << "the stored entry was not loaded";

  gSystem->Chmod(readOnly.c_str(), 0644);
  delete loaded;
  delete nav;
  ngnt->Close();
  delete ngnt;
  std::remove(readOnly.c_str());
}

/** Store, then load: the loaded navigator is whole and behaves like the one that was stored. */
TEST_F(NReshapeCacheTest, StoredNavigatorLoadsBackUsable)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  auto key = KeyFor(ngnt, {{0}, {1, 2}});

  Ndmspc::NGnNavigator * fresh = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(fresh);
  ASSERT_FALSE(fresh->GetChildren().empty());

  fresh->Export("cache_before.json", {});
  const std::string expected = ReadFile("cache_before.json");

  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(key, fresh)) << "storing the navigator failed";
  // These tests use the shared suite file, which is writable, so the entry went into it — check the
  // home that was actually used rather than assuming the cache directory.
  const auto stored = json::parse(Ndmspc::NReshapeCache::List(key.file));
  ASSERT_EQ(stored.size(), 1u) << "no entry was written: " << stored.dump();

  // A second key for the same combination must find it.
  auto again = KeyFor(ngnt, {{0}, {1, 2}});
  Ndmspc::NGnNavigator * loaded = Ndmspc::NReshapeCache::Load(again, ngnt);
  ASSERT_NE(loaded, nullptr) << "the stored navigator was not loaded";

  EXPECT_EQ(loaded->GetChildren().size(), fresh->GetChildren().size());
  EXPECT_NE(loaded->GetProjection(), nullptr) << "the projection did not survive the file";
  EXPECT_EQ(loaded->GetGnTree(), ngnt) << "Load should attach the tree it was given";

  // And it must still produce the same export - the check that it is genuinely usable, not just present.
  loaded->Export("cache_after.json", {});
  EXPECT_EQ(ReadFile("cache_after.json"), expected) << "the loaded navigator differs from the stored one";

  std::remove("cache_before.json");
  std::remove("cache_after.json");
  delete loaded;
  delete fresh;
  ngnt->Close();
  delete ngnt;
}

/** A miss returns nothing, and a disabled cache never writes. */
TEST_F(NReshapeCacheTest, DisabledCacheNeitherReadsNorWrites)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOff("NDMSPC_RESHAPE_CACHE", "0");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  auto key = KeyFor(ngnt, {{0}, {1, 2}});

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);

  EXPECT_FALSE(Ndmspc::NReshapeCache::Enabled());
  EXPECT_FALSE(Ndmspc::NReshapeCache::Store(key, nav)) << "a disabled cache must not write";
  EXPECT_EQ(Ndmspc::NReshapeCache::Load(key, ngnt), nullptr) << "a disabled cache must not read";

  delete nav;
  ngnt->Close();
  delete ngnt;
}

/**
 * An entry this build cannot interpret is dropped rather than served.
 *
 * A read-only source is used so the cache directory is the only home: with a writable file the load
 * would (correctly) find the in-file entry first and never reach the junk one.
 */
TEST_F(NReshapeCacheTest, StaleEntryIsDropped)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string readOnly = "test_NReshapeCache_unusable.root";
  NSingleBinning01Gaus(readOnly);
  ASSERT_EQ(gSystem->Chmod(readOnly.c_str(), 0444), 0);

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(readOnly);
  ASSERT_TRUE(ngnt);
  auto key = Ndmspc::NReshapeCache::Key().FromPath(readOnly);
  key.binning   = "";
  key.levels    = {{0}, {1, 2}};
  key.cacheFile = Ndmspc::NReshapeCache::CacheFilePath(readOnly);

  const std::string path = Ndmspc::NReshapeCache::Path(key);
  ASSERT_FALSE(path.empty());

  // The mirror lives under the cache root, so its directory has to be made here: this test writes the file
  // by hand rather than through `Store`, which is what normally creates it. Without this the open fails on
  // a fresh tree — and only there, since a previous run's directory made it look fine.
  {
    const std::string parent = gSystem->GetDirName(path.c_str()).Data();
    ASSERT_FALSE(parent.empty());
    // `CreateDirectory` reports failure for a directory that already exists, so ask whether it is usable
    // afterwards rather than whether this call made it.
    Ndmspc::NUtils::CreateDirectory(parent);
    ASSERT_FALSE(gSystem->AccessPathName(parent.c_str())) << "cannot create " << parent;
  }

  // An entry whose metadata is missing: exactly what an older or foreign writer would leave. It has to sit
  // where a real entry sits — `reshape/<digest>/` — for the load to find it and judge it.
  {
    TFile * out = TFile::Open(path.c_str(), "RECREATE");
    ASSERT_TRUE(out);
    out->cd();
    auto * reshape = out->mkdir("reshape");
    ASSERT_TRUE(reshape);
    reshape->cd();
    auto * entry = reshape->mkdir(Ndmspc::NReshapeCache::Digest(key).c_str());
    ASSERT_TRUE(entry);
    entry->cd();
    TNamed junk("nav", "not a navigator");
    junk.Write();
    out->Write();
    out->Close();
    delete out;
  }

  EXPECT_EQ(Ndmspc::NReshapeCache::Load(key, ngnt), nullptr) << "an entry with no metadata must not be served";
  {
    TFile * in = TFile::Open(path.c_str(), "READ");
    ASSERT_TRUE(in);
    EXPECT_EQ(in->Get(Ndmspc::NReshapeCache::EntryDirectory(Ndmspc::NReshapeCache::Digest(key)).c_str()),
              nullptr)
        << "the unusable entry should have been dropped";
    in->Close();
    delete in;
  }
  gSystem->Unlink(path.c_str());

  gSystem->Chmod(readOnly.c_str(), 0644);
  ngnt->Close();
  delete ngnt;
  std::remove(readOnly.c_str());
}

/**
 * A rewritten source file leaves its old entries unreachable: the digest covers the file's size and
 * mtime, so nothing can ever name them again. Pruning removes exactly those and keeps the live ones.
 */
TEST_F(NReshapeCacheTest, PruneDropsSupersededEntriesAndKeepsLiveOnes)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  auto first  = KeyFor(ngnt, {{0}, {1, 2}});
  auto second = KeyFor(ngnt, {{0, 1}, {2}});

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", first.levels);
  ASSERT_TRUE(nav);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(first, nav));
  Ndmspc::NGnNavigator * nav2 = ngnt->Reshape("", second.levels);
  ASSERT_TRUE(nav2);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(second, nav2));

  ASSERT_EQ(json::parse(Ndmspc::NReshapeCache::List(first.file)).size(), 2u);

  // Nothing is stale yet, so a prune must not touch anything: this is the case that would make the
  // cache delete its own live entries if the identity were hashed wrongly.
  auto clean = Ndmspc::NReshapeCache::Prune(first.file);
  EXPECT_EQ(clean.removed, 0u) << "a prune with nothing stale must not delete";
  EXPECT_EQ(clean.kept, 2u);
  EXPECT_EQ(json::parse(Ndmspc::NReshapeCache::List(first.file)).size(), 2u);

  delete nav2;
  delete nav;
  ngnt->Close();
  delete ngnt;
}

/**
 * The case prune exists for: an entry built from a version of the source that has since changed.
 *
 * The key is a content fingerprint, so an entry built when the tree held different data has a digest no
 * request can name again once the data changes. This builds exactly such an entry (by fingerprinting the
 * file as it was) and checks the prune recognises it.
 *
 * Note what a fingerprint does and does not catch: it follows the tree's entry and byte counts, so data
 * that is genuinely replaced is caught, while rewriting a file without changing what the tree contains
 * is not. That is the deliberate trade for an entry being storable inside the file at all.
 */
TEST_F(NReshapeCacheTest, PruneDropsEntriesBuiltFromDifferentSourceContent)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  // A throwaway tree of its own, so nothing else in the suite depends on it.
  const std::string own = "test_NReshapeCache_rewritten.root";
  NSingleBinning01Gaus(own);

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(own);
  ASSERT_TRUE(ngnt);
  auto current = KeyFor(ngnt, {{0}, {1, 2}});

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", current.levels);
  ASSERT_TRUE(nav);

  // The live entry, as it is now.
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(current, nav));
  ASSERT_EQ(json::parse(Ndmspc::NReshapeCache::List(current.file)).size(), 1u);

  auto clean = Ndmspc::NReshapeCache::Prune(current.file);
  EXPECT_EQ(clean.removed, 0u) << "the entry matching the file as it is must be kept";
  EXPECT_EQ(clean.kept, 1u);
  EXPECT_EQ(json::parse(Ndmspc::NReshapeCache::List(current.file)).size(), 1u);

  // An entry as it would have been built from a tree that held more data. Its digest cannot be named
  // again now the file holds what it holds, so it is exactly what a stale entry looks like.
  auto stale_key       = current;
  stale_key.nEntries   = current.nEntries + 1;
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(stale_key, nav));
  EXPECT_EQ(json::parse(Ndmspc::NReshapeCache::List(current.file)).size(), 2u);

  const auto stale = Ndmspc::NReshapeCache::Prune(current.file);
  EXPECT_EQ(stale.removed, 1u) << "the entry built from other content should have been pruned";
  EXPECT_EQ(stale.kept, 1u) << "the live entry must survive";
  // Nothing is reported as freed: an in-file entry shares its file with the data, so removing it
  // releases no file. Only a cache-directory entry is its own file and can report bytes.
  EXPECT_EQ(stale.bytesFreed, 0);
  // The *entry* is gone, not the file: an in-file entry lives in the tree file itself.
  {
    TFile * in = TFile::Open(current.file.c_str(), "READ");
    ASSERT_TRUE(in);
    EXPECT_EQ(in->Get(Ndmspc::NReshapeCache::EntryDirectory(Ndmspc::NReshapeCache::Digest(stale_key)).c_str()),
              nullptr)
        << "the stale entry is gone from disk";
    in->Close();
    delete in;
  }

  // And the live one is still loadable, which is the thing a prune must never cost.
  Ndmspc::NGnNavigator * loaded = Ndmspc::NReshapeCache::Load(KeyFor(ngnt, {{0}, {1, 2}}), ngnt);
  EXPECT_NE(loaded, nullptr) << "the prune removed the entry that was still valid";

  delete loaded;
  delete nav;
  ngnt->Close();
  delete ngnt;
  std::remove(own.c_str());
}

/** A prune leaves an entry whose source file cannot be read: its staleness cannot be shown. */
TEST_F(NReshapeCacheTest, PruneKeepsEntriesOfAMissingSourceFile)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  auto key = KeyFor(ngnt, {{0}, {1, 2}});
  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(key, nav));

  // A second, real file, whose entry is then made unreachable by removing the file: an entry whose source
  // cannot be read cannot be shown to be stale, so a prune must leave it alone.
  const std::string second = "test_NReshapeCache_gone.root";
  ASSERT_EQ(gSystem->CopyFile(testFile.c_str(), second.c_str()), 0);

  Ndmspc::NReshapeCache::Key other;
  other.FromPath(second);
  other.binning   = "";
  other.levels    = {{0}, {1, 2}};
  other.cacheFile = Ndmspc::NReshapeCache::CacheFilePath(second);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(other, nav));

  ASSERT_EQ(gSystem->Unlink(second.c_str()), 0);

  const auto stats = Ndmspc::NReshapeCache::Prune(second);
  EXPECT_EQ(stats.removed, 0u) << "an entry whose source is unreadable must not be pruned";
  EXPECT_EQ(stats.kept, 1u);
  EXPECT_FALSE(gSystem->AccessPathName(Ndmspc::NReshapeCache::Path(other).c_str()));
  gSystem->Unlink(Ndmspc::NReshapeCache::Path(other).c_str());

  delete nav;
  ngnt->Close();
  delete ngnt;
}
TEST_F(NReshapeCacheTest, ListNamesStoredCombinationsForOneFile)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);

  // Nothing stored yet for a path nobody used.
  EXPECT_EQ(json::parse(Ndmspc::NReshapeCache::List("/nonexistent/nothing.root")).size(), 0u);

  auto first = KeyFor(ngnt, {{0}, {1, 2}});
  auto second = KeyFor(ngnt, {{0, 1}, {2}});

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", first.levels);
  ASSERT_TRUE(nav);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(first, nav));

  Ndmspc::NGnNavigator * nav2 = ngnt->Reshape("", second.levels);
  ASSERT_TRUE(nav2);
  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(second, nav2));

  const json listed = json::parse(Ndmspc::NReshapeCache::List(first.file));
  ASSERT_EQ(listed.size(), 2u) << listed.dump();

  // Each entry identifies the combination it holds, so a client can offer it.
  std::vector<std::string> digests;
  for (const auto & entry : listed) {
    EXPECT_EQ(entry["file"], first.file);
    EXPECT_TRUE(entry.contains("levels")) << entry.dump();
    EXPECT_GT(entry["nChildren"].get<int>(), 0) << entry.dump();
    digests.push_back(entry["digest"]);
  }
  EXPECT_NE(std::find(digests.begin(), digests.end(), Ndmspc::NReshapeCache::Digest(first)), digests.end());
  EXPECT_NE(std::find(digests.begin(), digests.end(), Ndmspc::NReshapeCache::Digest(second)), digests.end());

  // A different file's combinations are not offered here.
  EXPECT_EQ(json::parse(Ndmspc::NReshapeCache::List("/nonexistent/other.root")).size(), 0u);

  delete nav2;
  delete nav;
  ngnt->Close();
  delete ngnt;
}

/** A file's sha256, so "the input was not touched" is checked byte for byte rather than by size. */
static std::string Fingerprint(const std::string & path)
{
  const TString out = gSystem->GetFromPipe(TString::Format("sha256sum %s | cut -d' ' -f1", path.c_str()));
  return std::string(out.Data()).substr(0, 64);
}

/** A local input keeps its entries in itself; a remote one defaults to the cache directory. */
TEST_F(NReshapeCacheTest, DefaultHomeFollowsTheKindOfInput)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);

  EXPECT_EQ(Ndmspc::NReshapeCache::DefaultCacheFile("/work/a/data.root"), "/work/a/data.root")
      << "a local file is its own default home: the cache travels with the data";
  EXPECT_EQ(Ndmspc::NReshapeCache::DefaultCacheFile("data.root"), "data.root");

  EXPECT_EQ(Ndmspc::NReshapeCache::DefaultCacheFile("root://eos.example.com//eos/a/data.root"), "")
      << "a remote input defaults to the cache directory: a default must not write to shared storage";
  EXPECT_EQ(Ndmspc::NReshapeCache::DefaultCacheFile("alien:///alice/data/a.root"), "");
  EXPECT_EQ(Ndmspc::NReshapeCache::DefaultCacheFile("http://example.com/a.root"), "");
  EXPECT_EQ(Ndmspc::NReshapeCache::DefaultCacheFile(""), "");

  EXPECT_FALSE(Ndmspc::NReshapeCache::IsRemote("/work/a/data.root"));
  EXPECT_FALSE(Ndmspc::NReshapeCache::IsRemote("file:///work/a/data.root")) << "file:// is local storage";
  EXPECT_TRUE(Ndmspc::NReshapeCache::IsRemote("root://eos.example.com//eos/a/data.root"));
}

/**
 * A cache file that exists but is empty is treated as "write a new one", not as "update it".
 *
 * This is what the lock leaves behind: the first writer of a cache file creates it so both racers lock the
 * same inode, and the file is zero bytes when `Store` then opens it. ROOT cannot open an empty file `UPDATE`
 * — there is no header to read — so existence alone is the wrong test and the store would report failure
 * on exactly the first entry into a fresh cache file.
 */
TEST_F(NReshapeCacheTest, AnEmptyCacheFileIsWrittenNotUpdated)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string fresh = cacheDir + "/fresh/empty-then-written.root";
  {
    const std::string parent = gSystem->GetDirName(fresh.c_str()).Data();
    Ndmspc::NUtils::CreateDirectory(parent);
    FILE * f = std::fopen(fresh.c_str(), "w");
    ASSERT_NE(f, nullptr) << "cannot create " << fresh;
    std::fclose(f);
  }
  FileStat_t st{};
  ASSERT_EQ(gSystem->GetPathInfo(fresh.c_str(), st), 0);
  ASSERT_EQ(st.fSize, 0) << "the home is supposed to start empty";

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
  ASSERT_TRUE(ngnt);
  auto key = KeyFor(ngnt, {{0}, {1, 2}});
  key.cacheFile = fresh;

  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);
  EXPECT_TRUE(Ndmspc::NReshapeCache::Store(key, nav)) << "an empty home must be written, not updated";

  ASSERT_EQ(gSystem->GetPathInfo(fresh.c_str(), st), 0);
  EXPECT_GT(st.fSize, 0) << "the cache file is still empty, so the store wrote nothing";

  // And it reads back: the entry is really in a valid ROOT file.
  Ndmspc::NGnNavigator * loaded = Ndmspc::NReshapeCache::Load(key, ngnt);
  ASSERT_NE(loaded, nullptr) << "the entry stored into an empty home must load back";

  delete loaded;
  delete nav;
  ngnt->Close();
  delete ngnt;
  gSystem->Unlink(fresh.c_str());
}

/** The cache file mirrors the input's path, so same-named inputs never share one. */
TEST_F(NReshapeCacheTest, CacheFileMirrorsTheInputPath)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);

  const std::string local = Ndmspc::NReshapeCache::CacheFilePath("/work/a/data.root");
  EXPECT_EQ(local, cacheDir + "/work/a/data.root") << "a local path keeps its directory structure";

  const std::string remote = Ndmspc::NReshapeCache::CacheFilePath("root://eos.example.com//eos/a/data.root");
  EXPECT_EQ(remote, cacheDir + "/eos.example.com/eos/a/data.root")
      << "a URL keeps its host as well, so two servers cannot collide";

  // The whole point of mirroring: the basename alone does not identify a file.
  const std::string one = Ndmspc::NReshapeCache::CacheFilePath("/work/a/data.root");
  const std::string two = Ndmspc::NReshapeCache::CacheFilePath("/work/b/data.root");
  EXPECT_NE(one, two) << "two files named data.root must not share a cache file";

  const std::string other = Ndmspc::NReshapeCache::CacheFilePath("root://other.example.com//eos/a/data.root");
  EXPECT_NE(remote, other) << "the same path on another host is another file";
}

/** Storing in the cache directory leaves the input byte-identical; storing in-file changes it. */
TEST_F(NReshapeCacheTest, StoreInTheCacheDirectoryLeavesTheInputUntouched)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string copy = "test_NReshapeCache_untouched.root";
  ASSERT_EQ(gSystem->CopyFile(testFile.c_str(), copy.c_str()), 0);
  const std::string before = Fingerprint(copy);

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(copy);
  ASSERT_TRUE(ngnt);

  auto key = KeyFor(ngnt, {{0}, {1, 2}});
  key.cacheFile = ""; // the cache directory, explicitly
  Ndmspc::NGnNavigator * nav = ngnt->Reshape("", key.levels);
  ASSERT_TRUE(nav);

  ASSERT_TRUE(Ndmspc::NReshapeCache::Store(key, nav));
  EXPECT_EQ(Fingerprint(copy), before) << "the input must be byte-identical when the cache is kept elsewhere";
  EXPECT_FALSE(gSystem->AccessPathName(Ndmspc::NReshapeCache::CacheFilePath(key.file).c_str()))
      << "the mirrored cache file holds the entry";

  // And it is found again from that home, with the same key.
  Ndmspc::NGnNavigator * loaded = Ndmspc::NReshapeCache::Load(key, ngnt);
  ASSERT_TRUE(loaded) << "an entry in the cache file must load back";
  EXPECT_EQ(loaded->GetNLevels(), nav->GetNLevels());

  delete loaded;
  delete nav;
  ngnt->Close();
  delete ngnt;
  std::remove(copy.c_str());
}

/**
 * The four modes are the 2x2 of "may I read the cache?" and "may I write it?".
 *
 * Each one is checked against a home that is emptied first, so what a mode stored — or refused to store — is
 * visible as a change in the entry count rather than inferred from a log line.
 */
TEST_F(NReshapeCacheTest, CacheModesDoWhatTheySay)
{
  ScopedEnvVar cacheDirEnv("NDMSPC_CACHE_DIR", cacheDir);
  ScopedEnvVar cacheOn("NDMSPC_RESHAPE_CACHE", "1");

  const std::string copy = "test_NReshapeCache_modes.root";
  ASSERT_EQ(gSystem->CopyFile(testFile.c_str(), copy.c_str()), 0);

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(copy);
  ASSERT_TRUE(ngnt);

  using Mode = Ndmspc::NReshapeCache::CacheMode;

  const std::vector<std::vector<int>> levels = {{0}, {1, 2}};
  auto                                key    = KeyFor(ngnt, levels);

  // The cache file, so what this test stores is visible in one place and the input stays untouched.
  key.cacheFile = Ndmspc::NReshapeCache::CacheFilePath(key.file);

  // The home this test works in holds one cache file, so emptying it is one unlink.
  auto reset = [&]() { gSystem->Unlink(key.cacheFile.c_str()); };
  auto storedCount = [&]() {
    // Only the entries in this test's own home: the input may carry entries from other tests.
    auto   listed = json::parse(Ndmspc::NReshapeCache::List(key.file));
    size_t n      = 0;
    for (const auto & entry : listed) {
      if (entry["home"] == "cache") n++;
    }
    return n;
  };
  auto run = [&](Mode mode) {
    bool                    built = false;
    Ndmspc::NGnNavigator *  nav =
        Ndmspc::NReshapeCache::LoadOrReshape(ngnt, levels, &built, mode, "");
    return std::make_pair(nav, built);
  };

  // Use: reads, and a miss is **not** kept.
  reset();
  {
    auto [nav, built] = run(Mode::kUse);
    ASSERT_TRUE(nav);
    EXPECT_TRUE(built) << "an empty home is a miss, so use reshapes";
    delete nav;
    EXPECT_EQ(storedCount(), 0u) << "use must not store what it had to reshape";
  }

  // UseOrStore: reads, and a miss is kept — so the second request does not reshape.
  reset();
  {
    auto [nav, built] = run(Mode::kUseOrStore);
    ASSERT_TRUE(nav);
    EXPECT_TRUE(built);
    delete nav;
    EXPECT_EQ(storedCount(), 1u) << "use-or-store keeps what it reshaped";

    auto [again, builtAgain] = run(Mode::kUseOrStore);
    ASSERT_TRUE(again);
    EXPECT_FALSE(builtAgain) << "use-or-store must load the entry it stored";
    delete again;
  }

  // Rebuild: does not read, and does store.
  reset();
  {
    auto [nav, built] = run(Mode::kUseOrStore);
    ASSERT_TRUE(nav);
    delete nav;
    ASSERT_EQ(storedCount(), 1u);

    auto [rebuiltNav, builtAgain] = run(Mode::kRebuild);
    ASSERT_TRUE(rebuiltNav);
    EXPECT_TRUE(builtAgain) << "rebuild must reshape even though an entry is stored";
    delete rebuiltNav;
    EXPECT_EQ(storedCount(), 1u) << "rebuild replaces the entry rather than adding one";
  }

  // Ignore: neither reads nor writes.
  reset();
  {
    auto [nav, built] = run(Mode::kUseOrStore);
    ASSERT_TRUE(nav);
    delete nav;
    ASSERT_EQ(storedCount(), 1u);

    auto [ignored, builtAgain] = run(Mode::kIgnore);
    ASSERT_TRUE(ignored);
    EXPECT_TRUE(builtAgain) << "ignore never reads, so it always reshapes";
    delete ignored;
    EXPECT_EQ(storedCount(), 1u) << "ignore leaves the stored entry as it was";
  }

  // A mode name round-trips, and the old spellings still mean what they did.
  EXPECT_TRUE(Ndmspc::NReshapeCache::Reads(Mode::kUse));
  EXPECT_TRUE(Ndmspc::NReshapeCache::Writes(Mode::kUseOrStore));
  EXPECT_FALSE(Ndmspc::NReshapeCache::Reads(Mode::kRebuild));
  EXPECT_FALSE(Ndmspc::NReshapeCache::Writes(Mode::kIgnore));

  Mode parsed = Mode::kIgnore;
  EXPECT_TRUE(Ndmspc::NReshapeCache::ParseMode("", parsed));
  EXPECT_EQ(parsed, Mode::kUseOrStore) << "an omitted mode fills the cache";
  EXPECT_TRUE(Ndmspc::NReshapeCache::ParseMode("refresh", parsed));
  EXPECT_EQ(parsed, Mode::kRebuild);
  EXPECT_TRUE(Ndmspc::NReshapeCache::ParseMode("skip", parsed));
  EXPECT_EQ(parsed, Mode::kIgnore);
  EXPECT_FALSE(Ndmspc::NReshapeCache::ParseMode("sometimes", parsed));

  reset();
  ngnt->Close();
  delete ngnt;
  std::remove(copy.c_str());
}
