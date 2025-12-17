
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "01_executor/NExecutor1D.C"
#include "01_executor/NExecutor2D.C"
#include "01_executor/NExecutor5D.C"

TEST(NExecutor1DTest, CreatesOutputFile)
{
  std::string testFile = "test_NExecutor1D.root";
  // Remove file if it exists
  std::remove(testFile.c_str());

  // Run the function
  NExecutor1D(testFile);

  // Check that the file was created
  std::ifstream f(testFile);
  ASSERT_TRUE(f.good()) << "Output file was not created";
  f.close();

  // Clean up
  std::remove(testFile.c_str());
}

TEST(NExecutor2DTest, CreatesOutputFile)
{
  std::string testFile = "test_NExecutor2D.root";
  // Remove file if it exists
  std::remove(testFile.c_str());

  // Run the function
  NExecutor2D(testFile);

  // Check that the file was created
  std::ifstream f(testFile);
  ASSERT_TRUE(f.good()) << "Output file was not created";
  f.close();

  // Clean up
  std::remove(testFile.c_str());
}

TEST(NExecutor5DTest, CreatesOutputFile)
{
  std::string testFile = "test_NExecutor2D.root";
  // Remove file if it exists
  std::remove(testFile.c_str());

  // Run the function
  NExecutor5D(testFile);

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
