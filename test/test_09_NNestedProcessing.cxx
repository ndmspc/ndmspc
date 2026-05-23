#include <gtest/gtest.h>
#include <filesystem>
#include <cstdlib>
#include <TSystem.h>

#include "07_nested_processing/NNestedProcessing01Gaus.C"
#include "../core/NGnTree.h"

using namespace std::filesystem;

TEST(NGnTreeNestingCounter, IncDecWorks)
{
  int before = Ndmspc::NGnTree::GetProcessNesting();
  Ndmspc::NGnTree::IncProcessNesting();
  ASSERT_EQ(Ndmspc::NGnTree::GetProcessNesting(), before + 1);
  Ndmspc::NGnTree::DecProcessNesting();
  ASSERT_EQ(Ndmspc::NGnTree::GetProcessNesting(), before);
}

TEST(NNestedProcessingMacro, CreatesRootUnderOutputDir)
{
  const std::string outDir = "/tmp/test_NNestedProcessing";
  const std::string outFile = "test_nested.root";

  // cleanup previous run
  std::error_code ec;
  std::filesystem::remove_all(outDir, ec);

  // Set per-level envs to exercise nesting selection
  gSystem->Setenv("NDMSPC_EXECUTION_MODE", "ipc:ipc");
  gSystem->Setenv("NDMSPC_MAX_PROCESSES", "2:3");

  // Run the macro (should not throw)
  NNestedProcessing01Gaus(outDir, outFile, false);

  // Look for any .root file under outDir
  bool found = false;
  if (exists(outDir)) {
    for (const auto & entry : recursive_directory_iterator(outDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".root") {
        found = true;
        break;
      }
    }
  }

  // Clean up
  std::filesystem::remove_all(outDir, ec);

  ASSERT_TRUE(found) << "No .root files produced under " << outDir;
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
