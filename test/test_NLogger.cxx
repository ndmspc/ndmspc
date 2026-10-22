#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ndmspc/core/NLogger.h"

using namespace Ndmspc;

TEST(NLoggerTest, KeepsTheNewestLinesOldestFirst)
{
  NLogInfo("logger-test-line-%d", 1);
  NLogInfo("logger-test-line-%d", 2);

  const std::vector<std::string> lines = NLogger::RecentLines(2);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_NE(lines[0].find("logger-test-line-1"), std::string::npos) << lines[0];
  EXPECT_NE(lines[1].find("logger-test-line-2"), std::string::npos) << lines[1];
  // The line carries the same shape the console would show.
  EXPECT_NE(lines[0].find("[INFO]"), std::string::npos) << lines[0];

  // Asking for everything never asks for more than is kept, and a limit larger than the ring is
  // clamped to it rather than reading past the front.
  EXPECT_FALSE(NLogger::RecentLines(0).empty());
  EXPECT_GE(NLogger::RecentLines(0).size(), 2u);
  EXPECT_LE(NLogger::RecentLines(100000).size(), 1000u);
}

TEST(NLoggerTest, SendsOnlyWhatIsNewSinceACursor)
{
  NLogger::ClearRecentLines();
  NLogInfo("diff-line-%d", 1);
  NLogInfo("diff-line-%d", 2);

  unsigned long long last  = 0;
  bool               reset = false;
  const std::vector<std::string> tail = NLogger::LogSince(0, 100, last, reset);
  ASSERT_EQ(tail.size(), 2u);
  EXPECT_TRUE(reset);
  EXPECT_NE(tail[1].find("diff-line-2"), std::string::npos) << tail[1];

  // Nothing new since the cursor: no lines, and the cursor does not move.
  const unsigned long long cursor = last;
  EXPECT_TRUE(NLogger::LogSince(cursor, 100, last, reset).empty());
  EXPECT_FALSE(reset);
  EXPECT_EQ(last, cursor);

  // One new line: exactly that line, as a diff to append.
  NLogInfo("diff-line-%d", 3);
  const std::vector<std::string> diff = NLogger::LogSince(cursor, 100, last, reset);
  ASSERT_EQ(diff.size(), 1u);
  EXPECT_FALSE(reset);
  EXPECT_NE(diff[0].find("diff-line-3"), std::string::npos) << diff[0];

  // A clear keeps the cursor meaningful: only lines logged afterwards are sent.
  const unsigned long long beforeClear = last;
  NLogger::ClearRecentLines();
  EXPECT_TRUE(NLogger::LogSince(beforeClear, 100, last, reset).empty());
  NLogInfo("diff-line-%d", 4);
  const std::vector<std::string> afterClear = NLogger::LogSince(beforeClear, 100, last, reset);
  ASSERT_EQ(afterClear.size(), 1u);
  EXPECT_NE(afterClear[0].find("diff-line-4"), std::string::npos) << afterClear[0];
}

TEST(NLoggerTest, CanBeCleared)
{
  NLogInfo("logger-clear-test");
  ASSERT_FALSE(NLogger::RecentLines(0).empty());

  NLogger::ClearRecentLines();
  EXPECT_TRUE(NLogger::RecentLines(0).empty());

  // Logging carries on after a clear.
  NLogInfo("logger-clear-test-after");
  EXPECT_FALSE(NLogger::RecentLines(0).empty());
}
