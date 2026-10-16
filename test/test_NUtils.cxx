/**
 * The macro list the CLIs hand to `-m`.
 *
 * A list may mix a directory's macros with paths elsewhere and remote URLs, so a bare name — one that
 * names no directory of its own — is resolved against the directory the entry before it named. That is
 * what lets a deployment write `-m "<dir>/one.C,two.C"` and have both come from `<dir>`.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "ndmspc/core/NUtils.h"

using Ndmspc::NUtils;

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
