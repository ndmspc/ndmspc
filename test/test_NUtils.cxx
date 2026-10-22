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

  EXPECT_EQ(NUtils::ResolveMacroList(local, "/usr/share/ndmspc/macros/tools"),
            (std::vector<std::string>{local}));
  EXPECT_EQ(NUtils::ResolveMacroList("test_ResolveMacroList_missing.C", "/usr/share/ndmspc/macros/tools"),
            (std::vector<std::string>{"/usr/share/ndmspc/macros/tools/test_ResolveMacroList_missing.C"}));

  std::remove(local.c_str());
}

TEST(ResolveMacroList, ABareNameTakesTheDirectoryOfTheEntryBeforeIt)
{
  EXPECT_EQ(NUtils::ResolveMacroList("/usr/share/ndmspc/macros/tools/toolBrowser.C,toolNgnt.C"),
            (std::vector<std::string>{"/usr/share/ndmspc/macros/tools/toolBrowser.C",
                                      "/usr/share/ndmspc/macros/tools/toolNgnt.C"}));
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
  EXPECT_EQ(NUtils::ResolveMacroList("toolNgnt.C", "/usr/share/ndmspc/macros/tools"),
            (std::vector<std::string>{"/usr/share/ndmspc/macros/tools/toolNgnt.C"}));
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
