/**
 * The macro list the CLIs hand to `-m`.
 *
 * A list may mix a directory's macros with paths elsewhere and remote URLs, so a bare name — one that
 * names no directory of its own — is resolved against the directory the entry before it named. That is
 * what lets a deployment write `-m "<dir>/one.C,two.C"` and have both come from `<dir>`.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <TROOT.h>
#include <utility>
#include <vector>

#include "ndmspc/core/NUtils.h"

using Ndmspc::NUtils;

namespace {

std::string MakeTempDir()
{
  char tmpl[] = "/tmp/ndmspc-precompiled-XXXXXX";
  char * dir = mkdtemp(tmpl);
  return dir != nullptr ? std::string(dir) : std::string();
}

void WriteFile(const std::string & path, const std::string & content)
{
  std::ofstream out(path);
  out << content;
}

std::pair<long long, long long> SizeAndMtime(const std::string & path)
{
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) return {0, 0};
  return {static_cast<long long>(st.st_size), static_cast<long long>(st.st_mtime)};
}

std::string Manifest(const std::string & name, long long size, long long mtime, int root, int version = 1)
{
  return "{\"version\":" + std::to_string(version) + ",\"root\":" + std::to_string(root) + ",\"macros\":{\"" + name +
         ".C\":{\"library\":\"" + name + "_C.so\",\"entry\":\"" + name + "\",\"size\":" + std::to_string(size) +
         ",\"mtime\":" + std::to_string(mtime) + "}}}";
}

} // namespace

TEST(NdmspcRoot, ResolvesEnvInOrder)
{
  const char *      savedRoot = std::getenv("NDMSPC_ROOT");
  const char *      savedHome = std::getenv("NDMSPC__HOME");
  const char *      savedDir  = std::getenv("NDMSPC_DIR");
  const std::string root      = savedRoot ? savedRoot : "";
  const std::string home      = savedHome ? savedHome : "";
  const std::string dir       = savedDir ? savedDir : "";

  unsetenv("NDMSPC_ROOT");
  unsetenv("NDMSPC__HOME");
  unsetenv("NDMSPC_DIR");
  EXPECT_EQ(NUtils::NdmspcRoot(), "/usr/share/ndmspc");

  setenv("NDMSPC_DIR", "/opt/ndmspc", 1);
  EXPECT_EQ(NUtils::NdmspcRoot(), "/opt/ndmspc/share/ndmspc");

  setenv("NDMSPC__HOME", "/custom/home", 1);
  EXPECT_EQ(NUtils::NdmspcRoot(), "/custom/home");

  setenv("NDMSPC_ROOT", "/custom/root", 1);
  EXPECT_EQ(NUtils::NdmspcRoot(), "/custom/root");

  auto restore = [](const char * name, const std::string & value) {
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
  };
  restore("NDMSPC_ROOT", root);
  restore("NDMSPC__HOME", home);
  restore("NDMSPC_DIR", dir);
}

TEST(ResolveMacroList, ABareNameTakesTheLocalFileWhenThereIsOne)
{
  // A file beside the caller is run as named - `ndmspc-run sum.C` runs the macro in the current
  // directory - and the tool directory the caller named is only the fallback for a name nothing local
  // answers to.
  const std::string local = "test_ResolveMacroList_local.C";
  {
    std::ofstream out(local);
    out << "// local macro\n";
  }

  EXPECT_EQ(NUtils::ResolveMacroList(local, "/usr/share/ndmspc/tools"),
            (std::vector<std::string>{local}));
  EXPECT_EQ(NUtils::ResolveMacroList("test_ResolveMacroList_missing.C", "/usr/share/ndmspc/tools"),
            (std::vector<std::string>{"/usr/share/ndmspc/tools/test_ResolveMacroList_missing.C"}));

  std::remove(local.c_str());
}

TEST(ResolveMacroList, ABareNameTakesTheDirectoryOfTheEntryBeforeIt)
{
  EXPECT_EQ(NUtils::ResolveMacroList("/usr/share/ndmspc/tools/toolBrowser.C,toolNgnt.C"),
            (std::vector<std::string>{"/usr/share/ndmspc/tools/toolBrowser.C",
                                      "/usr/share/ndmspc/tools/toolNgnt.C"}));
}

TEST(ResolveMacroList, APathEntrySetsTheDirectoryForWhatFollows)
{
  EXPECT_EQ(NUtils::ResolveMacroList("/a/one.C,/b/two.C,three.C,four.C"),
            (std::vector<std::string>{"/a/one.C", "/b/two.C", "/b/three.C", "/b/four.C"}));
}

TEST(ResolveMacroList, ARemoteDirectoryCarriesToo)
{
  EXPECT_EQ(NUtils::ResolveMacroList("https://example.org/macros/one.C,two.C"),
            (std::vector<std::string>{"https://example.org/macros/one.C",
                                      "https://example.org/macros/two.C"}));
}

TEST(ResolveMacroList, ABareNameWithNothingBeforeItFallsBackOnTheDefaultDirectory)
{
  EXPECT_EQ(NUtils::ResolveMacroList("toolNgnt.C", "/usr/share/ndmspc/tools"),
            (std::vector<std::string>{"/usr/share/ndmspc/tools/toolNgnt.C"}));
  // With no default directory either, the name is left as it is.
  EXPECT_EQ(NUtils::ResolveMacroList("toolNgnt.C"), (std::vector<std::string>{"toolNgnt.C"}));
}

TEST(ResolveMacroList, EmptyEntriesAreDropped)
{
  EXPECT_EQ(NUtils::ResolveMacroList("/d/one.C,,two.C,"),
            (std::vector<std::string>{"/d/one.C", "/d/two.C"}));
}

TEST(ResolveMacroList, APathWithoutADirectoryComponentIsABareName)
{
  // "dir/one.C" names a directory (its own), so it is used as it is; a name with no slash at all does
  // not, and takes the directory in hand.
  EXPECT_EQ(NUtils::ResolveMacroList("sub/one.C,two.C", "/base"),
            (std::vector<std::string>{"sub/one.C", "sub/two.C"}));
}

TEST(PrecompiledMacro, ACurrentManifestNamesTheLibraryAndEntry)
{
  const std::string dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  const std::string macro = dir + "/toolTest.C";
  WriteFile(macro, "void toolTest() {}\n");
  const auto [size, mtime] = SizeAndMtime(macro);

  const std::string cache = dir + "/.precompiled/" + std::to_string(gROOT->GetVersionInt());
  std::filesystem::create_directories(cache);
  WriteFile(cache + "/toolTest_C.so", "not a real library");
  WriteFile(cache + "/precompiled.json", Manifest("toolTest", size, mtime, gROOT->GetVersionInt()));

  std::string entry;
  EXPECT_EQ(NUtils::PrecompiledMacro(macro, entry), cache + "/toolTest_C.so");
  EXPECT_EQ(entry, "toolTest");

  std::filesystem::remove_all(dir);
}

TEST(PrecompiledMacro, AStaleMacroFallsBackToInterpretation)
{
  const std::string dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  const std::string macro = dir + "/toolTest.C";
  WriteFile(macro, "void toolTest() {}\n");
  const auto [size, mtime] = SizeAndMtime(macro);

  const std::string cache = dir + "/.precompiled/" + std::to_string(gROOT->GetVersionInt());
  std::filesystem::create_directories(cache);
  WriteFile(cache + "/toolTest_C.so", "not a real library");
  WriteFile(cache + "/precompiled.json", Manifest("toolTest", size + 1, mtime, gROOT->GetVersionInt()));

  std::string entry;
  EXPECT_EQ(NUtils::PrecompiledMacro(macro, entry), "");
  EXPECT_EQ(entry, "");

  WriteFile(cache + "/precompiled.json", Manifest("toolTest", size, mtime, gROOT->GetVersionInt() - 1));
  EXPECT_EQ(NUtils::PrecompiledMacro(macro, entry), "");

  WriteFile(cache + "/precompiled.json", Manifest("toolTest", size, mtime, gROOT->GetVersionInt(), 2));
  EXPECT_EQ(NUtils::PrecompiledMacro(macro, entry), "");

  WriteFile(cache + "/precompiled.json", Manifest("toolTest", size, mtime, gROOT->GetVersionInt()));
  std::filesystem::remove(cache + "/toolTest_C.so");
  EXPECT_EQ(NUtils::PrecompiledMacro(macro, entry), "");

  std::filesystem::remove_all(dir);
}

TEST(PrecompiledMacro, RemoteMacrosNeverUseACache)
{
  std::string entry;
  EXPECT_EQ(NUtils::PrecompiledMacro("https://example.org/toolTest.C", entry), "");
  EXPECT_EQ(NUtils::PrecompiledMacro("root://example.org//toolTest.C", entry), "");
}

TEST(ExecMacro, WithoutACacheTheMacroIsInterpreted)
{
  const std::string dir = MakeTempDir();
  ASSERT_FALSE(dir.empty());
  const std::string macro = dir + "/fallback.C";
  const std::string marker = dir + "/fallback-ran";
  WriteFile(macro, "#include <fstream>\nvoid fallback() { std::ofstream(\"" + marker + "\") << \"ran\"; }\n");

  EXPECT_TRUE(NUtils::ExecMacro(macro));
  EXPECT_TRUE(std::filesystem::exists(marker));

  std::filesystem::remove_all(dir);
}

namespace {

const std::string kHost = "eos.ndmspc.io";
/** The plain path `eos find` prints — no `root://<host>` prefix; the result carries that. */
const std::string kBase    = "/eos/ndmspc/scratch/ndmspc/demo/alice/rsn/import/systematics_multi";
const std::string kTargets = "/eos/ndmspc/scratch/ndmspc/demo/alice/rsn/import/systematics_flat";
const std::string kRoot    = "root://" + kHost;

/** What `eos find -f --name PAR_ngnt.root <base>` prints: a plain path, or a symlink's `path -> target`. */
std::string Listing(const std::string & name, const std::string & target = "")
{
  return kBase + "/" + name + "/PAR_ngnt.root" + (target.empty() ? "" : " -> " + target);
}

/** The URL the finder answers with for a path it printed (`root://<host>/` before the path's own `/`). */
std::string Url(const std::string & path)
{
  return kRoot + "/" + path;
}

} // namespace

TEST(EosFindPaths, ASymlinkIsItsPathAndNotItsTarget)
{
  // The finder was asked which paths under the base match the name. A symlink answers with both halves,
  // and the target is where it points — possibly outside the base altogether — so only the path is kept.
  const std::string listing = Listing("dca_0p05/default", kTargets + "/dca_cut-dca_0p05/PAR_ngnt.root");

  EXPECT_EQ(NUtils::EosFindPaths(listing, kHost),
            (std::vector<std::string>{Url(kBase + "/dca_0p05/default/PAR_ngnt.root")}));
}

TEST(EosFindPaths, APlainPathIsKeptWhole)
{
  EXPECT_EQ(NUtils::EosFindPaths(Listing("default/default"), kHost),
            (std::vector<std::string>{Url(kBase + "/default/default/PAR_ngnt.root")}));
}

TEST(EosFindPaths, EveryLineIsOnePathInTheOrderPrinted)
{
  const std::string listing = Listing("dca_0p05") + "\n" + Listing("one", kTargets + "/a") + "\n" +
                              Listing("two", kTargets + "/b") + "\n" + Listing("ncls_60", kTargets + "/c");

  EXPECT_EQ(NUtils::EosFindPaths(listing, kHost),
            (std::vector<std::string>{Url(kBase + "/dca_0p05/PAR_ngnt.root"),
                                      Url(kBase + "/one/PAR_ngnt.root"),
                                      Url(kBase + "/two/PAR_ngnt.root"),
                                      Url(kBase + "/ncls_60/PAR_ngnt.root")}));
}

TEST(EosFindPaths, BlanksAndCarriageReturnsAreDropped)
{
  // The listing comes off the wire, so a line may carry the CR of a CRLF, and an empty line names nothing.
  EXPECT_EQ(NUtils::EosFindPaths("  " + Listing("one") + "  \r\n\n\t" + Listing("two") + "\r\n", kHost),
            (std::vector<std::string>{Url(kBase + "/one/PAR_ngnt.root"),
                                      Url(kBase + "/two/PAR_ngnt.root")}));
}

TEST(EosFindPaths, AnEmptyListingNamesNothing)
{
  EXPECT_EQ(NUtils::EosFindPaths("", kHost), (std::vector<std::string>{}));
  EXPECT_EQ(NUtils::EosFindPaths("  \n\t\r\n", kHost), (std::vector<std::string>{}));
}

TEST(LabelsFromDirectory, ReadsOneLabelPerAxisInAxesOrder)
{
  // The layout `NParamsImportMulti.C` imports: one directory per axis, then the file itself.
  const std::vector<std::string> paths = {
      kBase + "/dca_0p05/default/vtxz_8/PAR_ngnt.root",
      kBase + "/dca_0p5/tpc_2/vtxz_10/PAR_ngnt.root",
  };
  const std::vector<std::string> axes = {"dca", "pid", "vtxz"};

  EXPECT_EQ(NUtils::LabelsFromDirectory(paths, kBase, "PAR_ngnt.root", axes),
            (std::vector<std::vector<std::string>>{{"dca_0p05", "default", "vtxz_8"},
                                                  {"dca_0p5", "tpc_2", "vtxz_10"}}));
}

TEST(LabelsFromDirectory, KeepsTheOrderThePathsCameIn)
{
  const std::vector<std::string> paths = {kBase + "/one/x/PAR_ngnt.root", kBase + "/two/y/PAR_ngnt.root",
                                          kBase + "/three/z/PAR_ngnt.root"};

  const auto labels = NUtils::LabelsFromDirectory(paths, kBase, "PAR_ngnt.root", {"a", "b"});
  EXPECT_EQ(labels.size(), 3u);
  EXPECT_EQ(labels[0][0], "one");
  EXPECT_EQ(labels[2][0], "three");
}

TEST(LabelsFromDirectory, SkipsAPathThatNamesNoLabelPerAxis)
{
  // A path with the wrong depth describes no point of this tree, exactly as AxesFromDirectory skips it.
  const std::vector<std::string> paths = {kBase + "/a/b/PAR_ngnt.root", kBase + "/a/PAR_ngnt.root"};
  const auto labels = NUtils::LabelsFromDirectory(paths, kBase, "PAR_ngnt.root", {"x", "y"});

  EXPECT_EQ(labels, (std::vector<std::vector<std::string>>{{"a", "b"}}));
}

TEST(LabelsFromDirectory, AnEmptyListYieldsNothing)
{
  EXPECT_EQ(NUtils::LabelsFromDirectory({}, kBase, "PAR_ngnt.root", {"x"}),
            (std::vector<std::vector<std::string>>{}));
}
