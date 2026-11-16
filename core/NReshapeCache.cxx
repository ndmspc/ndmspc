#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <TDirectory.h>
#include <TFile.h>
#include <TKey.h>
#include <TTree.h>
#include <TSystem.h>

#include "NGnTree.h"
#include "NLogger.h"
#include "NReshapeCache.h"
#include "NUtils.h"

// No `ClassImp`: `NReshapeCache` is a plain utility with only static methods, never streamed and never looked
// up by name. `ClassImp` without a matching `ClassDef` and a LinkDef entry registers a class ROOT then has no
// dictionary for, which is what produced the teardown noise
// "An object of type TStreamerInfo ... was truncated (due a missing dictionary)".

namespace Ndmspc {

namespace {

/**
 * The layout version of an entry.
 *
 * Bumped whenever a stored navigator's streamed layout changes, which is exactly when an entry written
 * by an older library must not be loaded: the objects in it would be read with the new layout. This is
 * deliberately **not** the project version — a recompile that changes nothing about the navigator
 * should not throw the cache away.
 */
constexpr int kCacheFormat = 1;

/// The object name of the navigator inside an entry directory.
constexpr const char * kNavName = "nav";
/// The object name of the metadata inside an entry directory.
constexpr const char * kMetaName = "meta";
/// The object name of the kept map export inside an entry directory.
constexpr const char * kMapJsonName = "mapjson";

/**
 * A stable hex digest of a string (FNV-1a, 64 bit).
 *
 * `std::hash` is deliberately not used: it is not specified to be stable across implementations or even
 * runs, and a cache entry name has to be the same in every process and every release.
 */
std::string Fnv1a(const std::string & text)
{
  unsigned long long hash = 1469598103934665603ULL; // offset basis
  for (unsigned char c : text) {
    hash ^= static_cast<unsigned long long>(c);
    hash *= 1099511628211ULL; // FNV prime
  }
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", hash);
  return std::string(buf);
}

/// A levels vector the way the digest and the metadata spell it: `[[0],[1,2]]`.
std::string FormatLevels(const std::vector<std::vector<int>> & levels)
{
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < levels.size(); i++) {
    if (i > 0) ss << ",";
    ss << "[";
    for (size_t j = 0; j < levels[i].size(); j++) {
      if (j > 0) ss << ",";
      ss << levels[i][j];
    }
    ss << "]";
  }
  ss << "]";
  return ss.str();
}

/// The digest of a set of inputs, as text.
std::string DigestParts(const std::string & file, long long nEntries, long long totBytes,
                        const std::string & binning, const std::string & levels, bool averages)
{
  std::stringstream ss;
  ss << "v" << kCacheFormat << "|" << file << "|" << nEntries << "|" << totBytes << "|" << binning << "|"
     << levels << "|" << (averages ? 1 : 0);
  return Fnv1a(ss.str());
}

/// The absolute path of a file, so the same file reached two ways is one combination.
std::string AbsolutePath(const std::string & path)
{
  if (path.empty()) return "";

  // A URL is already absolute and must be left exactly as given: prefixing the working directory onto
  // `root://host//path` produces a nonsense local path, which then reads as "not writable" and silently
  // sends every remote source to the fallback cache instead of storing the entry in the tree.
  if (path.find("://") != std::string::npos) return path;

  char *      resolved = gSystem->ExpandPathName(TString(path.c_str()));
  std::string out      = resolved != nullptr ? resolved : path;
  delete[] resolved;
  if (!out.empty() && out[0] != '/') out = std::string(gSystem->pwd()) + "/" + out;
  return out;
}

/// The content fingerprint of a tree file, read from the tree header only. {0,0} when unreadable.
void FingerprintPath(const std::string & path, long long & nEntries, long long & totBytes)
{
  nEntries = 0;
  totBytes = 0;
  if (path.empty()) return;

  TFile * in = TFile::Open(path.c_str(), "READ");
  if (in == nullptr || in->IsZombie()) {
    delete in;
    return;
  }
  auto * tree = dynamic_cast<TTree *>(in->Get("ngnt"));
  if (tree != nullptr) {
    nEntries = static_cast<long long>(tree->GetEntries());
    totBytes = static_cast<long long>(tree->GetTotBytes());
  }
  in->Close();
  delete in;
}

/*
 * The metadata an entry holds is a flat JSON object written by BuildMeta, so reading a field back does
 * not need a parser: find the key, then take the value that follows. This keeps NReshapeCache free of a
 * JSON dependency and, more importantly, lets a listing skip a field it does not recognise.
 */

/// The string value of `key` in a flat JSON object, or "" when absent.
std::string ExtractJsonString(const std::string & json, const std::string & key)
{
  const std::string pattern = "\"" + key + "\":\"";
  const auto        at      = json.find(pattern);
  if (at == std::string::npos) return "";
  const auto start = at + pattern.size();
  const auto end   = json.find('"', start);
  if (end == std::string::npos) return "";
  return json.substr(start, end - start);
}

/// The raw (non-string) value of `key` in a flat JSON object, or "" when absent.
std::string ExtractJsonRaw(const std::string & json, const std::string & key)
{
  const std::string pattern = "\"" + key + "\":";
  const auto        at      = json.find(pattern);
  if (at == std::string::npos) return "";
  const auto start = at + pattern.size();
  // A raw value ends at the next ',' or '}', except that a nested array/object may contain both.
  size_t i       = start;
  int    nesting = 0;
  while (i < json.size()) {
    const char c = json[i];
    if (c == '[' || c == '{') nesting++;
    else if (c == ']' || c == '}') {
      if (nesting == 0) break;
      nesting--;
    }
    else if (c == ',' && nesting == 0) break;
    i++;
  }
  return json.substr(start, i - start);
}

/// One entry found somewhere: what it claims to be, and where it is.
struct Entry {
  std::string path;              ///< The .root file holding it (the tree file, or a cache file)
  std::string home;              ///< "in-file" or "cache"
  std::string digest;            ///< The combination's digest
  std::string file;              ///< The source file the entry was built from
  std::string binning;
  std::string levelsRaw;         ///< As written: `[[0],[1,2]]`
  std::string averagesRaw;       ///< "true" / "false"
  std::string createdRaw;
  std::string nLevelsRaw;
  std::string nChildrenRaw;
  std::string meta;              ///< The whole metadata object
  long long   bytes{0};
  bool        usable{false};     ///< Written in this build's format
};

/// Read an entry's metadata out of an open directory. Everything but path/home/bytes.
bool ReadEntryMeta(TDirectory * dir, Entry & entry)
{
  if (dir == nullptr) return false;
  auto * metaObj = dynamic_cast<TNamed *>(dir->Get(kMetaName));
  if (metaObj == nullptr) return false;

  entry.meta         = metaObj->GetTitle();
  entry.file         = ExtractJsonString(entry.meta, "file");
  entry.binning      = ExtractJsonString(entry.meta, "binning");
  entry.levelsRaw    = ExtractJsonRaw(entry.meta, "levels");
  entry.averagesRaw  = ExtractJsonRaw(entry.meta, "averages");
  entry.createdRaw   = ExtractJsonRaw(entry.meta, "created");
  entry.nLevelsRaw   = ExtractJsonRaw(entry.meta, "nLevels");
  entry.nChildrenRaw = ExtractJsonRaw(entry.meta, "nChildren");

  const std::string tag = "\"format\":" + std::to_string(kCacheFormat);
  entry.usable          = entry.meta.find(tag) != std::string::npos;
  return true;
}

/// Every entry inside one tree file, walked without reading any navigator.
std::vector<Entry> ScanTreeFile(const std::string & path, const std::string & home = "in-file")
{
  std::vector<Entry> entries;
  if (path.empty()) return entries;

  TFile * in = TFile::Open(path.c_str(), "READ");
  if (in == nullptr || in->IsZombie()) {
    delete in;
    return entries;
  }

  auto * reshape = dynamic_cast<TDirectory *>(in->Get("reshape"));
  if (reshape == nullptr) {
    in->Close();
    delete in;
    return entries;
  }

  for (TObject * obj : *reshape->GetListOfKeys()) {
    auto * key = dynamic_cast<TKey *>(obj);
    if (key == nullptr) continue;
    if (TString(key->GetClassName()) != "TDirectoryFile") continue;

    const std::string digest = key->GetName();
    auto *            dir    = dynamic_cast<TDirectory *>(reshape->Get(digest.c_str()));
    if (dir == nullptr) continue;

    Entry entry;
    if (!ReadEntryMeta(dir, entry)) continue;
    entry.path   = path;
    entry.home   = home;
    entry.digest = digest;
    entries.push_back(entry);
  }

  in->Close();
  delete in;
  return entries;
}

/// Every entry in the server-side cache directory, reading metadata only.
///
/// Walks **recursively**: a cache file mirrors its input's path (`<base>/<host>/<dir>/<file>`), so the
/// entries are nested rather than flat beside the base directory.
std::vector<Entry> ScanCacheDirectory(const std::string & dir)
{
  std::vector<Entry> entries;
  if (dir.empty()) return entries;

  void * handle = gSystem->OpenDirectory(dir.c_str());
  if (handle == nullptr) return entries;

  std::vector<std::string> names;
  while (const char * name = gSystem->GetDirEntry(handle)) {
    const std::string entry = name;
    if (entry == "." || entry == "..") continue;
    names.push_back(entry);
  }
  gSystem->FreeDirectory(handle);

  for (const std::string & name : names) {
    const std::string path = dir + "/" + name;

    // A directory is one step of the mirrored path: descend. (`GetPathInfo` says so without opening it.)
    FileStat_t st;
    if (gSystem->GetPathInfo(path.c_str(), st) == 0 && (st.fMode & kS_IFDIR)) {
      for (auto & nested : ScanCacheDirectory(path)) entries.push_back(nested);
      continue;
    }
    if (name.size() < 6 || name.compare(name.size() - 5, 5, ".root") != 0) continue;

    Entry entry;
    entry.path   = path;
    entry.home   = "cache";
    entry.digest = name.substr(0, name.size() - 5);

    TFile * in = TFile::Open(entry.path.c_str(), "READ");
    if (in == nullptr || in->IsZombie()) {
      delete in;
      continue;
    }
    if (ReadEntryMeta(in, entry)) {
      FileStat_t st;
      if (gSystem->GetPathInfo(entry.path.c_str(), st) == 0) entry.bytes = static_cast<long long>(st.fSize);
      entries.push_back(entry);
    }
    in->Close();
    delete in;
  }
  return entries;
}

/**
 * Make `parent/name` current, creating it when needed.
 *
 * ROOT resolves `cd()` and `mkdir()` against the CURRENT directory, so a path like `"reshape/<digest>"`
 * called from inside `reshape` looks for `reshape/reshape/<digest>` and fails — silently, leaving a
 * write to land in the wrong place. This steps one component at a time and reports failure.
 */
TDirectory * EnterOrMake(TDirectory * parent, const std::string & name)
{
  if (parent == nullptr || name.empty()) return nullptr;
  auto *       existing = dynamic_cast<TDirectory *>(parent->Get(name.c_str()));
  TDirectory * dir      = existing != nullptr ? existing : parent->mkdir(name.c_str());
  if (dir == nullptr) return nullptr;
  return dir->cd() ? dir : nullptr;
}

/// Read the navigator out of an entry directory, handing it out of the file's ownership.
NGnNavigator * ReadEntryNav(TDirectory * dir)
{
  if (dir == nullptr) return nullptr;
  auto * nav = dynamic_cast<NGnNavigator *>(dir->Get(kNavName));
  if (nav == nullptr) return nullptr;
  // An object read through a TFile is owned by its directory: take it out before the file closes, or
  // it is freed with the file. NGnNavigator is not a TH1 and has no SetDirectory, so the directory's
  // own list is what is edited.
  if (dir->GetList() != nullptr) dir->GetList()->Remove(nav);
  return nav;
}

/// A scoped exclusive lock on a file, so two writers cannot interleave inside `TFile`'s UPDATE window.
///
/// ROOT's `UPDATE` open is a read-modify-write of the whole file: the second writer reads the directory as
/// it was before the first one's `Write()`, then writes its own version back, dropping whatever the first
/// stored. Two precache processes on one tree therefore lose an entry silently — both exit 0 and one
/// combination is simply gone (measured: 5 rounds, an entry lost every time).
///
/// The lock is taken on the **data file itself**, which was verified not to block a reader: `flock` is on
/// the open descriptor, and ROOT's `TFile::Open` for reading does not take it. It is deliberately not a
/// `<file>.lock` sidecar — that would leave a permanent file beside the user's data.
///
/// Locking the target itself also means two processes never contend by accident: `flock` is per inode, so
/// different cache files are different locks, and only writers of the *same* home queue up. That is what
/// makes two pipelines sharing one host (and one `HOME`-derived cache) safe.
///
/// **A file that does not exist yet is created**, because the commonest contest is two processes storing the
/// first entry into the same cache file: with a plain `O_RDONLY` open the missing file made this class warn
/// and carry on **unlocked**, which is exactly the interleaving it exists to prevent. The created file is
/// left empty for the caller to fill — `Store` treats a zero-length home as "needs RECREATE" rather than
/// opening `UPDATE` on a file with no ROOT header.
///
/// **Readers are unaffected** and stay lock-free: they open READ only, so there is nothing to race. Only
/// writers take this lock.
class FileLock {
  public:
  explicit FileLock(const std::string & path)
  {
    // A URL cannot be locked with flock; a remote store is left to the storage element.
    if (path.empty() || path.find("://") != std::string::npos) return;

    // Read-only first: `flock` needs a descriptor, not write access, and this keeps the lock usable on a
    // file this user may read but not write (a read-only input, the in-file home).
    fFd = ::open(path.c_str(), O_RDONLY);
    if (fFd < 0 && errno == ENOENT) {
      // Not there yet: create it so both racers lock the same inode instead of both running unlocked. This
      // only ever applies to a cache file under the cache directory, never to the input.
      fFd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    }
    if (fFd < 0) {
      NLogWarning("NReshapeCache: cannot lock '%s' (continuing unlocked)", path.c_str());
      return;
    }
    // Blocking: the point is to wait for the other writer rather than lose an entry. Contention lasts as
    // long as one update, which is milliseconds.
    if (::flock(fFd, LOCK_EX) != 0) {
      NLogWarning("NReshapeCache: cannot take the lock on '%s' (continuing unlocked)", path.c_str());
      ::close(fFd);
      fFd = -1;
    }
  }
  ~FileLock()
  {
    if (fFd < 0) return;
    ::flock(fFd, LOCK_UN);
    ::close(fFd);
  }
  FileLock(const FileLock &)            = delete;
  FileLock & operator=(const FileLock &) = delete;

  private:
  int fFd{-1};
};

/// Store the pieces of an entry into an already-current directory.
bool WriteEntryHere(NGnNavigator * nav, const std::string & metaJson, const std::string & mapJson)
{
  if (nav == nullptr) return false;
  nav->Write(kNavName);
  TNamed metaObj(kMetaName, metaJson.c_str());
  metaObj.Write();
  if (!mapJson.empty()) {
    TNamed mapObj(kMapJsonName, mapJson.c_str());
    mapObj.Write();
  }
  return true;
}

} // namespace

std::string NReshapeCache::EntryDirectory(const std::string & digest)
{
  return "reshape/" + digest;
}

NReshapeCache::Key & NReshapeCache::Key::FromTree(NGnTree * tree)
{
  if (tree == nullptr || tree->GetStorageTree() == nullptr) return *this;

  file = AbsolutePath(tree->GetStorageTree()->GetFileName());
  if (file.empty()) return *this;

  // The fingerprint comes from the tree header, never from the file's size or mtime: writing an entry
  // into the file changes those, and an entry keyed on them would invalidate itself as it was stored.
  // Read the fingerprint from the FILE, not from the in-memory tree. A live TTree reports a slightly
  // different totBytes than the same tree read back (it depends on what has been set up on the object),
  // so fingerprinting the live tree gave one digest here and another to any later process that opened
  // the file - the entry would be written and then never found again.
  FingerprintPath(file, nEntries, totBytes);
  return *this;
}

NReshapeCache::Key & NReshapeCache::Key::FromPath(const std::string & path)
{
  file = AbsolutePath(path);
  FingerprintPath(file, nEntries, totBytes);
  return *this;
}

std::string NReshapeCache::Digest(const Key & key)
{
  return DigestParts(key.file, key.nEntries, key.totBytes, key.binning, FormatLevels(key.levels), key.averages);
}

std::string NReshapeCache::Directory()
{
  // The **base** of the cache, not a per-entry directory: entries now live in a per-input mirror file under
  // it (`CacheFilePath`), so appending "reshape" here would nest a second one inside every mirror.
  std::string  base;
  const char * env = gSystem->Getenv("NDMSPC_CACHE_DIR");
  if (env != nullptr && env[0] != '\0') {
    base = env;
  }
  else {
    const char * home = gSystem->Getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      NLogWarning("NReshapeCache: no NDMSPC_CACHE_DIR and no HOME; caching is skipped");
      return "";
    }
    base = std::string(home) + "/.cache/ndmspc";
  }
  // `NUtils::CreateDirectory` reports failure for a directory that already exists (TSystem::mkdir
  // returns non-zero for EEXIST), so ask whether the directory is usable rather than whether it was
  // just created — every call after the first would otherwise claim caching was unavailable.
  if (gSystem->AccessPathName(base.c_str(), kWritePermission)) {
    if (!NUtils::CreateDirectory(base) || gSystem->AccessPathName(base.c_str(), kWritePermission)) {
      NLogWarning("NReshapeCache: cannot create '%s'; caching is skipped", base.c_str());
      return "";
    }
  }
  return base;
}

bool NReshapeCache::Enabled()
{
  const char * env = gSystem->Getenv("NDMSPC_RESHAPE_CACHE");
  if (env == nullptr || env[0] == '\0') return true;
  return NUtils::ParseBoolEnv(env);
}

std::string NReshapeCache::Path(const Key & key)
{
  // The cache-file home for this key: the caller's choice when it names one, the mirror otherwise.
  if (!key.cacheFile.empty()) return key.cacheFile;
  return CacheFilePath(key.file);
}

bool NReshapeCache::IsRemote(const std::string & path)
{
  // `file://` is local storage wearing a scheme; every other scheme is somebody else's filesystem.
  const size_t scheme = path.find("://");
  if (scheme == std::string::npos) return false;
  return path.compare(0, scheme, "file") != 0;
}

std::string NReshapeCache::DefaultCacheFile(const std::string & file)
{
  // In-file for local data: cheap, and the cache travels with what it describes. The cache directory for
  // remote data: an `UPDATE` round-trip to a shared filesystem is not something a default should do, and the
  // caller may not be allowed to write it at all.
  if (file.empty() || IsRemote(file)) return "";
  return file;
}

std::string NReshapeCache::CacheFilePath(const std::string & file)
{
  if (file.empty()) return "";

  const std::string base = Directory();
  if (base.empty()) return "";

  // Mirror the input's path so one input always maps to one cache file: two `data.root` files in different
  // directories must not share a cache, which the old flat `<digest>.root` scheme could not express.
  if (IsRemote(file)) {
    TUrl url(file.c_str());
    std::string host = url.GetHost();
    std::string path = url.GetFile();
    if (path.empty()) return base + "/" + host + "/" + gSystem->BaseName(file.c_str());
    if (path.front() != '/') path = "/" + path;
    return base + "/" + host + path;
  }

  // A local path: keep its directory structure under the cache root.
  std::string local = AbsolutePath(file);
  if (local.front() != '/') local = "/" + local;
  return base + local;
}

std::string NReshapeCache::BuildMeta(const Key & key, NGnNavigator * nav)
{
  // A small, human-readable record. It is read back for the format check and to say what an entry
  // holds; nothing else depends on it, so it can gain fields without invalidating entries.
  std::stringstream ss;
  ss << "{"
     << "\"format\":" << kCacheFormat << ","
     << "\"file\":\"" << key.file << "\","
     << "\"nEntries\":" << key.nEntries << ","
     << "\"totBytes\":" << key.totBytes << ","
     << "\"binning\":\"" << key.binning << "\","
     << "\"levels\":" << FormatLevels(key.levels) << ","
     << "\"averages\":" << (key.averages ? "true" : "false") << ","
     << "\"nLevels\":" << (nav ? nav->GetNLevels() : 0) << ","
     << "\"nChildren\":" << (nav ? nav->GetChildren().size() : 0) << ","
     << "\"created\":" << static_cast<long long>(gSystem->Now()) << "}";
  return ss.str();
}

bool NReshapeCache::Store(const Key & key, NGnNavigator * nav)
{
  if (!Enabled() || nav == nullptr) return false;

  const std::string digest = Digest(key);
  const std::string meta   = BuildMeta(key, nav);

  // One home per request: `cacheFile` when the caller chose one, the mirror otherwise. `file` itself means
  // in-file; anything else is a separate cache file. The input is opened UPDATE only in the first case.
  const std::string home   = Path(key);
  const bool        inFile = !home.empty() && home == key.file;

  if (home.empty()) {
    NLogWarning("NReshapeCache: no cache home for '%s'; the reshape is returned uncached", key.file.c_str());
    return false;
  }

  // A separate cache file may not exist yet: make its directory, then open it UPDATE (which creates it).
  if (!inFile) {
    const std::string parent = gSystem->GetDirName(home.c_str()).Data();
    if (!parent.empty() && gSystem->AccessPathName(parent.c_str(), kWritePermission)) {
      if (!NUtils::CreateDirectory(parent)) {
        NLogWarning("NReshapeCache: cannot create '%s'; the reshape is returned uncached", parent.c_str());
        return false;
      }
    }
  }

  bool wrote = false;
  {
    // Held across the whole read-modify-write: two writers otherwise drop each other's entries.
    FileLock lock(home);

    // RECREATE for a fresh cache file, UPDATE for one that may already hold other combinations. "Fresh"
    // includes a **zero-length** file: the lock creates a home that did not exist, and ROOT cannot open an
    // empty file UPDATE (there is no header to read), so the size decides rather than existence.
    FileStat_t st{};
    const bool exists = gSystem->GetPathInfo(home.c_str(), st) == 0 && st.fSize > 0;
    TFile *    io     = TFile::Open(home.c_str(), exists ? "UPDATE" : "RECREATE");
    // A read-only input fails right here rather than at the write, so this is a "try the other home"
    // outcome, not a dead end.
    if (io == nullptr || io->IsZombie()) {
      delete io;
    }
    else {
      io->cd();
      TDirectory * reshapeDir = EnterOrMake(io, "reshape");
      TDirectory * entryDir   = EnterOrMake(reshapeDir, digest);
      // `io->Write()` is the step that reaches the disk, so its result is part of the outcome: a read-only
      // file accepts the objects in memory and then refuses the write, which once looked like success.
      wrote = entryDir != nullptr && WriteEntryHere(nav, meta, "") && io->Write() >= 0;
      io->Close();
      delete io;
    }
  }

  if (wrote) {
    NLogInfo("NReshapeCache: stored %s in '%s' (%s)", digest.c_str(), home.c_str(),
             inFile ? "in-file" : "cache file");
    return true;
  }

  // An in-file store can fail for a reason the caller cannot fix (a read-only mount, a file another writer
  // holds). The cache file still works, so an entry is never lost to that.
  if (inFile) {
    const std::string mirror = CacheFilePath(key.file);
    if (!mirror.empty() && mirror != home) {
      NLogInfo("NReshapeCache: '%s' is not writable; storing in '%s'", key.file.c_str(), mirror.c_str());
      Key fallback       = key;
      fallback.cacheFile = mirror;
      return Store(fallback, nav);
    }
  }

  NLogWarning("NReshapeCache: could not write '%s'; the reshape is returned uncached", home.c_str());
  return false;
}

NGnNavigator * NReshapeCache::Load(const Key & key, NGnTree * tree)
{
  if (!Enabled()) return nullptr;

  const std::string digest = Digest(key);
  NGnNavigator *    nav    = nullptr;
  std::string       foundIn;

  // The homes, in the order they are tried: the chosen one first — that is where a store would have put it
  // — then the cache file, then the input itself. All three are tried because a store falls back to the
  // cache file when the input cannot be written, and an entry written before a home was chosen is still an
  // entry for this combination.
  std::vector<std::string> homes;
  auto                     addHome = [&homes](const std::string & home) {
    if (home.empty()) return;
    if (std::find(homes.begin(), homes.end(), home) != homes.end()) return;
    homes.push_back(home);
  };
  addHome(Path(key));           // cacheFile when named, the mirror otherwise
  addHome(CacheFilePath(key.file)); // the mirror, in case the chosen home could not be written
  addHome(key.file);            // in-file

  for (const std::string & home : homes) {
    if (gSystem->AccessPathName(home.c_str()) == kFALSE) {
      TFile * in = TFile::Open(home.c_str(), "READ");
      if (in != nullptr && !in->IsZombie()) {
        auto * dir = dynamic_cast<TDirectory *>(in->Get(EntryDirectory(digest).c_str()));
        if (dir != nullptr) {
          // An entry written in another cache format is dropped rather than served: its streamed layout may
          // not match this build.
          auto *            metaObj = dynamic_cast<TNamed *>(dir->Get(kMetaName));
          const std::string tag     = "\"format\":" + std::to_string(kCacheFormat);
          const std::string meta = metaObj != nullptr ? metaObj->GetTitle() : "";
          if (meta.find(tag) == std::string::npos) {
            NLogInfo("NReshapeCache: '%s' holds an unusable entry for %s; dropping it", home.c_str(), digest.c_str());
            in->Close();
            delete in;
            Key stale      = key;
            stale.cacheFile = home;
            Remove(stale);
            continue;
          }
          nav = ReadEntryNav(dir);
          if (nav != nullptr) foundIn = (home == key.file) ? "the input file" : "'" + home + "'";
        }
      }
      if (in != nullptr) {
        in->Close();
        delete in;
      }
    }
    if (nav != nullptr) break;
  }

  if (nav == nullptr) return nullptr;

  if (tree != nullptr) {
    // A navigator's tree is transient, so a loaded one has none; give it the caller's, all the way
    // down, which is what makes it behave like a freshly reshaped navigator.
    nav->SetGnTreeRecursive(tree);
  }

  NLogInfo("NReshapeCache: loaded %s from %s", digest.c_str(), foundIn.c_str());
  return nav;
}

bool NReshapeCache::LoadMapJson(const Key & key, std::string & out)
{
  if (!Enabled()) return false;
  const std::string digest = Digest(key);

  auto readFrom = [&](TDirectory * dir) -> bool {
    if (dir == nullptr) return false;
    auto * obj = dynamic_cast<TNamed *>(dir->Get(kMapJsonName));
    if (obj == nullptr) return false;
    out = obj->GetTitle();
    return !out.empty();
  };

  // The map export lives beside its navigator, so it is looked for in the same homes, in the same order.
  std::vector<std::string> homes;
  const std::string        chosen = Path(key);
  if (!chosen.empty()) homes.push_back(chosen);
  if (!key.file.empty() && key.file != chosen) homes.push_back(key.file);

  for (const std::string & home : homes) {
    if (gSystem->AccessPathName(home.c_str())) continue;

    TFile * in = TFile::Open(home.c_str(), "READ");
    bool    ok = false;
    if (in != nullptr && !in->IsZombie()) {
      ok = readFrom(dynamic_cast<TDirectory *>(in->Get(EntryDirectory(digest).c_str())));
    }
    if (in != nullptr) {
      in->Close();
      delete in;
    }
    if (ok) return true;
  }
  return false;
}

bool NReshapeCache::StoreMapJson(const Key & key, const std::string & text)
{
  if (!Enabled() || text.empty()) return false;
  const std::string digest = Digest(key);

  // Beside the navigator it belongs to: the same home `Store` would pick. The entry directory already exists
  // there (the navigator was stored first), so this only adds the `mapjson` object to it.
  const std::string home = Path(key);
  if (home.empty() || gSystem->AccessPathName(home.c_str())) return false;

  FileLock lock(home);

  TFile * io = TFile::Open(home.c_str(), "UPDATE");
  if (io == nullptr || io->IsZombie()) {
    delete io;
    return false;
  }
  auto * dir = dynamic_cast<TDirectory *>(io->Get(EntryDirectory(digest).c_str()));
  if (dir == nullptr || !dir->cd()) {
    io->Close();
    delete io;
    return false;
  }
  TNamed mapObj(kMapJsonName, text.c_str());
  mapObj.Write();
  const bool ok = io->Write() >= 0;
  io->Close();
  delete io;
  return ok;
}

bool NReshapeCache::ParseMode(const std::string & name, CacheMode & mode)
{
  // `""` is the default: fill the cache as it is used.
  if (name.empty() || name == "use-or-store" || name == "useOrStore") {
    mode = CacheMode::kUseOrStore;
    return true;
  }
  if (name == "use") {
    mode = CacheMode::kUse;
    return true;
  }
  if (name == "rebuild" || name == "refresh") {
    // `refresh` is the old name for a rebuild.
    mode = CacheMode::kRebuild;
    return true;
  }
  if (name == "ignore" || name == "skip") {
    // `skip` is an older word for the same thing: build, keep nothing.
    mode = CacheMode::kIgnore;
    return true;
  }
  return false;
}

NReshapeCache::Key NReshapeCache::KeyFor(NGnTree * tree, const std::vector<std::vector<int>> & levels)
{
  Key key;
  if (tree == nullptr) return key;
  key.FromTree(tree);
  key.binning   = tree->GetBinning() != nullptr ? tree->GetBinning()->GetCurrentDefinitionName() : "";
  key.levels    = levels;
  key.cacheFile = DefaultCacheFile(key.file);
  return key;
}

NGnNavigator * NReshapeCache::LoadOrReshape(NGnTree * tree, const std::vector<std::vector<int>> & levels,
                                            bool *                            rebuilt, CacheMode mode,
                                            const std::optional<std::string> & cacheFile)
{
  if (rebuilt != nullptr) *rebuilt = false;
  if (tree == nullptr) return nullptr;

  Key key = KeyFor(tree, levels);
  if (cacheFile.has_value()) key.cacheFile = *cacheFile; // the caller's choice wins over the default home

  // The two questions the modes answer, asked once each: reading and writing are independent, which is why
  // the four modes are the whole truth and no fifth is needed.
  if (Reads(mode)) {
    if (NGnNavigator * stored = Load(key, tree)) return stored;
  }

  NGnNavigator * nav = tree->Reshape(key.binning, levels, 0, {}, {});
  if (nav == nullptr) return nullptr; // nothing is stored for a combination that could not be built

  // A miss under `Use` is a miss the caller does not want kept.
  if (Writes(mode)) Store(key, nav);

  if (rebuilt != nullptr) *rebuilt = true;
  return nav;
}

int NReshapeCache::CountStored(const std::string & file)
{
  if (file.empty()) return 0;

  TFile * in = TFile::Open(file.c_str(), "READ");
  if (in == nullptr || in->IsZombie()) {
    delete in;
    return 0;
  }

  int count = 0;
  if (auto * reshape = dynamic_cast<TDirectory *>(in->Get("reshape"))) {
    count = reshape->GetListOfKeys() != nullptr ? reshape->GetListOfKeys()->GetEntries() : 0;
  }
  in->Close();
  delete in;
  return count;
}

bool NReshapeCache::Remove(const Key & key)
{
  bool              removed = false;
  const std::string digest  = Digest(key);

  // Every home the entry could be in: the chosen one, and the input itself when that is different. An entry
  // written before a home was chosen must still be removable, or a prune would leave it behind.
  std::vector<std::string> homes;
  const std::string        chosen = Path(key);
  if (!chosen.empty()) homes.push_back(chosen);
  if (!key.file.empty() && key.file != chosen) homes.push_back(key.file);

  for (const std::string & home : homes) {
    if (gSystem->AccessPathName(home.c_str())) continue;

    FileLock lock(home);

    TFile * io = TFile::Open(home.c_str(), "UPDATE");
    if (io == nullptr || io->IsZombie()) {
      delete io;
      continue;
    }
    io->cd();
    auto * reshapeDir = dynamic_cast<TDirectory *>(io->Get("reshape"));
    if (reshapeDir != nullptr && reshapeDir->Get(digest.c_str()) != nullptr) {
      // The trailing ";*" is required: a bare name matches no key in TDirectory::Delete, which then does
      // nothing and reports nothing — a silent no-op that looks like a successful prune.
      reshapeDir->Delete(TString::Format("%s;*", digest.c_str()));
      const bool ok = reshapeDir->Get(digest.c_str()) == nullptr;
      if (ok) io->Write();
      removed = removed || ok;
    }
    io->Close();
    delete io;
  }
  return removed;
}

std::string NReshapeCache::List(const std::string & file)
{
  // Metadata only, never a navigator: a listing runs while a client decides what to show, and loading
  // every stored navigator to answer that would cost more than the reshape it is trying to avoid.
  std::vector<Entry> entries;
  const std::string  absolute = file.empty() ? "" : AbsolutePath(file);

  if (!absolute.empty()) {
    // The input's own entries (legacy), then its mirror file — one input maps to one cache file, so this is
    // the whole story for a file and no directory walk is needed.
    for (auto & entry : ScanTreeFile(absolute)) entries.push_back(entry);

    const std::string mirror = CacheFilePath(absolute);
    if (!mirror.empty() && mirror != absolute) {
      for (auto & entry : ScanTreeFile(mirror, "cache")) entries.push_back(entry);
    }
  }
  else {
    // No file named: everything the cache directory holds, walking its mirrored tree.
    for (auto & entry : ScanCacheDirectory(Directory())) entries.push_back(entry);
  }

  std::string out   = "[";
  bool        first = true;
  for (const Entry & entry : entries) {
    if (!entry.usable) continue;

    std::stringstream e;
    e << "{"
      << "\"digest\":\"" << entry.digest << "\","
      << "\"file\":\"" << entry.file << "\","
      << "\"binning\":\"" << entry.binning << "\","
      << "\"levels\":" << (entry.levelsRaw.empty() ? "[]" : entry.levelsRaw) << ","
      << "\"averages\":" << (entry.averagesRaw == "true" ? "true" : "false") << ","
      << "\"nLevels\":" << (entry.nLevelsRaw.empty() ? "0" : entry.nLevelsRaw) << ","
      << "\"nChildren\":" << (entry.nChildrenRaw.empty() ? "0" : entry.nChildrenRaw) << ","
      << "\"bytes\":" << entry.bytes << ","
      << "\"created\":" << (entry.createdRaw.empty() ? "0" : entry.createdRaw) << ","
      << "\"home\":\"" << entry.home << "\"}";

    if (!first) out += ",";
    out += e.str();
    first = false;
  }
  return out + "]";
}

NReshapeCache::PruneStats NReshapeCache::Prune(const std::string & file)
{
  PruneStats stats;

  // The fingerprint a combination would have TODAY, per source file, so an entry can be compared with
  // the file as it stands. A file that cannot be read yields no fingerprint, and its entries are kept:
  // staleness that cannot be shown is not acted on.
  struct FileNow {
    bool      readable{false};
    long long nEntries{0};
    long long totBytes{0};
  };
  std::map<std::string, FileNow> nowOf;
  auto                           nowFor = [&](const std::string & sourceFile) -> const FileNow & {
    auto it = nowOf.find(sourceFile);
    if (it != nowOf.end()) return it->second;
    FileNow now;
    FingerprintPath(sourceFile, now.nEntries, now.totBytes);
    now.readable = !(now.nEntries == 0 && now.totBytes == 0);
    return nowOf.emplace(sourceFile, now).first->second;
  };

  const std::string  absolute = file.empty() ? "" : AbsolutePath(file);
  std::vector<Entry> entries;
  if (!absolute.empty()) {
    // The input's own entries, then its mirror file — the same two homes `List` reads.
    for (auto & entry : ScanTreeFile(absolute)) entries.push_back(entry);

    const std::string mirror = CacheFilePath(absolute);
    if (!mirror.empty() && mirror != absolute) {
      for (auto & entry : ScanTreeFile(mirror, "cache")) entries.push_back(entry);
    }
  }
  else {
    for (auto & entry : ScanCacheDirectory(Directory())) entries.push_back(entry);
  }

  for (const Entry & entry : entries) {
    // An entry this build cannot read will never be loaded, whatever its source says.
    bool drop = !entry.usable;
    if (!drop) {
      const FileNow & now = nowFor(entry.file);
      if (!now.readable || entry.file.empty()) {
        stats.kept++;
        continue;
      }
      // The digest this entry would have if built from the file as it is now. Equal means it is the
      // current entry for its combination; otherwise a later version of the source superseded it.
      const std::string current = DigestParts(entry.file, now.nEntries, now.totBytes, entry.binning,
                                             entry.levelsRaw, entry.averagesRaw == "true");
      drop = (entry.digest != current);
    }
    if (!drop) {
      stats.kept++;
      continue;
    }

    // Both homes are `.root` files holding `reshape/<digest>/`, so an entry is removed the same way from
    // either — deleting the file itself would take every other combination with it. The UPDATE open is a
    // read-modify-write like any store, so a prune racing a store in another process would otherwise drop
    // the entry being written: the same lock, on the same home, for all writers.
    bool     removed = false;
    FileLock lock(entry.path);
    TFile *  io = TFile::Open(entry.path.c_str(), "UPDATE");
    if (io != nullptr && !io->IsZombie()) {
      io->cd();
      auto * reshape = dynamic_cast<TDirectory *>(io->Get("reshape"));
      if (reshape != nullptr) {
        // Bare names do not match in TDirectory::Delete; the ";*" is the cycle wildcard.
        reshape->Delete(TString::Format("%s;*", entry.digest.c_str()));
        removed = reshape->Get(entry.digest.c_str()) == nullptr;
        if (removed) io->Write();
      }
      io->Close();
      delete io;
    }
    else {
      delete io;
    }

    if (removed) {
      stats.removed++;
      stats.bytesFreed += entry.bytes;
      NLogInfo("NReshapeCache: pruned '%s/%s' (%s)", entry.path.c_str(), entry.digest.c_str(), entry.home.c_str());
    }
    else {
      stats.kept++;
    }
  }

  if (stats.removed > 0) {
    NLogInfo("NReshapeCache: prune removed %zu entries (%.1f kB), kept %zu", stats.removed,
             stats.bytesFreed / 1024.0, stats.kept);
  }
  return stats;
}

} // namespace Ndmspc
