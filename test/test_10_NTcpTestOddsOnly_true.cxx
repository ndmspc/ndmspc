#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <TFile.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Include the NTcpTest macro implementation so the function is compiled into this test
#include "NTcpTest.C"

namespace fs = std::filesystem;

TEST(NTcpTestOddsOnly_True, ProducesOutputFile)
{
  std::string out = "test_10_NTcpTestOddsOnly_true.root";
  pid_t pid = fork();
  if (pid == 0) {
    NTcpTest(out, "10", 0, true);
    _exit(0);
  } else {
    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "Child process failed";
    ASSERT_TRUE(fs::exists(out)) << "Expected output file not found: " << out;
    fs::remove(out);
  }
}
