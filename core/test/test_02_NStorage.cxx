
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "02_storage/NStorage01Gaus.C"

TEST(NStorage01GausTest, CreatesOutputFile)
{
  std::string testFile = "test_NStorage01Gaus.root";
  // Remove file if it exists
  std::remove(testFile.c_str());

  // Run the function
  NStorage01Gaus(testFile);

  // Check that the file was created
  std::ifstream f(testFile);
  ASSERT_TRUE(f.good()) << "Output file was not created";
  f.close();

  // Clean up
  std::remove(testFile.c_str());
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
