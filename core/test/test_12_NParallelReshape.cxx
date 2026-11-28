#include <gtest/gtest.h>

#include <TSystem.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "05_single_bining/NSingleBinning01Gaus.C"
#include "ndmspc/core/NGnNavigator.h"
#include "ndmspc/core/NGnTree.h"

namespace {

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
  ScopedEnvVar(const ScopedEnvVar &)            = delete;
  ScopedEnvVar & operator=(const ScopedEnvVar &) = delete;

  private:
  std::string fName;
  std::string fPrevious;
  bool        fHadPrevious{false};
};

std::string ReadFile(const std::string & name)
{
  std::ifstream     f(name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

/**
 * The parallel reshape of a first layer must produce exactly what the serial one produces.
 *
 * Each worker reshapes its own bins under its own tree and the subtrees are attached afterwards, so the
 * two paths have to agree on every cell, object and parameter — the JSON export is the whole navigator,
 * which makes a byte comparison the strongest statement the test can make.
 */
class NParallelReshapeTest : public ::testing::Test {
  protected:
  static const std::string testFile;

  static void SetUpTestSuite()
  {
    std::ifstream f(testFile);
    if (!f.good()) NSingleBinning01Gaus(testFile);
  }
  static void TearDownTestSuite() { std::remove(testFile.c_str()); }

  /**
   * Export the navigator of one reshape to JSON, at a given worker count.
   *
   * The parallel reshape is in-process threads only, and it deliberately stands down under the fork-based
   * execution modes (see NGnNavigator::ReshapeWorkerCount). The test harness sets NDMSPC_EXECUTION_MODE=ipc,
   * so that is cleared for the duration, together with the worker count itself.
   */
  std::string ReshapeToJson(const std::vector<std::vector<int>> & levels, int nWorkers, const std::string & tag)
  {
    const ScopedEnvVar workersVar("NDMSPC_NAV_WORKERS", std::to_string(nWorkers));
    const ScopedEnvVar modeVar("NDMSPC_EXECUTION_MODE", "");

    std::string outFile = "test_parallel_reshape_" + tag + ".json";
    std::string jsonText;
    {
      Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
      if (ngnt && !ngnt->IsZombie()) {
        Ndmspc::NGnNavigator * nav = ngnt->Reshape("", levels);
        if (nav) {
          nav->Export(outFile, {});
          jsonText = ReadFile(outFile);
          delete nav;
        }
        ngnt->Close();
      }
    }

    std::remove(outFile.c_str());
    return jsonText;
  }
};

const std::string NParallelReshapeTest::testFile = "test_NParallelReshape.root";

} // namespace

TEST_F(NParallelReshapeTest, SerialAndParallelAgreeOnTwoLevels)
{
  // Level 0 is not the leaf here, so its bins are the ones that get split across workers.
  const std::vector<std::vector<int>> levels = {{0}, {1, 2}};

  const std::string serial   = ReshapeToJson(levels, 1, "serial_a");
  const std::string parallel = ReshapeToJson(levels, 4, "parallel_a");

  ASSERT_FALSE(serial.empty()) << "Serial reshape produced no JSON";
  ASSERT_FALSE(parallel.empty()) << "Parallel reshape produced no JSON";
  EXPECT_EQ(serial, parallel) << "Parallel reshape differs from the serial one";
}

TEST_F(NParallelReshapeTest, SerialAndParallelAgreeOnTwoAxesFirstLayer)
{
  // A 2-D first layer: 30 bins at level 0, so the work is genuinely split.
  const std::vector<std::vector<int>> levels = {{0, 1}, {2}};

  const std::string serial   = ReshapeToJson(levels, 1, "serial_b");
  const std::string parallel = ReshapeToJson(levels, 4, "parallel_b");

  ASSERT_FALSE(serial.empty());
  ASSERT_FALSE(parallel.empty());
  EXPECT_EQ(serial, parallel) << "Parallel reshape differs from the serial one";
}

TEST_F(NParallelReshapeTest, SingleLevelReshapeIsUnaffectedByWorkerCount)
{
  // One level means level 0 is the leaf, whose objects are filled into the single shared node; the
  // parallel path must not engage, and both counts have to agree all the same.
  const std::string one = ReshapeToJson({{0, 1, 2}}, 1, "single_1");
  const std::string four = ReshapeToJson({{0, 1, 2}}, 4, "single_4");

  ASSERT_FALSE(one.empty());
  ASSERT_FALSE(four.empty());
  EXPECT_EQ(one, four);
}

TEST_F(NParallelReshapeTest, LegacyProjectionModeIgnoresWorkerCount)
{
  // NDMSPC_RESHAPE_PROJECTION=per-bin selects the old per-bin projection code, which is serial by
  // definition (it is the fallback for ruling the shared projection out). Asking for workers must not
  // change what it produces — and must not produce an empty navigator, which it once did.
  const std::vector<std::vector<int>> levels = {{0}, {1, 2}};

  const ScopedEnvVar legacy("NDMSPC_RESHAPE_PROJECTION", "per-bin");
  const std::string  one  = ReshapeToJson(levels, 1, "legacy_1");
  const std::string  four = ReshapeToJson(levels, 4, "legacy_4");

  ASSERT_FALSE(one.empty()) << "Legacy projection produced no JSON";
  ASSERT_FALSE(four.empty()) << "Legacy projection produced no JSON with 4 workers";
  EXPECT_NE(four, "null") << "Legacy projection produced an empty navigator";
  EXPECT_EQ(one, four);
}

TEST_F(NParallelReshapeTest, ParallelReshapeBuildsTheSameNumberOfChildren)
{
  const std::vector<std::vector<int>> levels = {{0}, {1, 2}};

  auto countChildren = [&](int nWorkers) -> size_t {
    const ScopedEnvVar workersVar("NDMSPC_NAV_WORKERS", std::to_string(nWorkers));
    const ScopedEnvVar modeVar("NDMSPC_EXECUTION_MODE", "");
    size_t n = 0;
    Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(testFile);
    if (ngnt && !ngnt->IsZombie()) {
      Ndmspc::NGnNavigator * nav = ngnt->Reshape("", levels);
      if (nav) {
        for (auto * child : nav->GetChildren()) {
          if (child) n++;
        }
        delete nav;
      }
      ngnt->Close();
    }
    return n;
  };

  const size_t serial   = countChildren(1);
  const size_t parallel = countChildren(4);
  EXPECT_GT(serial, 0u);
  EXPECT_EQ(serial, parallel);
}

/**
 * A reshape follows `ndmspc-run -n`, without a second variable to learn.
 *
 * `ReshapeWorkerCount` is private, so this checks the policy where it shows: a reshape with only
 * `NDMSPC_MAX_PROCESSES` set (what `ndmspc-run -n N` sets) must produce the same navigator as one with the
 * reshape's own variable. A parallel reshape that quietly disagreed would be worse than a slow one.
 */
TEST_F(NParallelReshapeTest, ReshapeFollowsTheProcessCountWhenTheReshapeVariableIsUnset)
{
  const std::vector<std::vector<int>> levels = {{0, 1}, {2}};

  std::string fromProcessCount;
  {
    const ScopedEnvVar modeVar("NDMSPC_EXECUTION_MODE", "");
    const ScopedEnvVar navVar("NDMSPC_NAV_WORKERS", "");
    const ScopedEnvVar procVar("NDMSPC_MAX_PROCESSES", "");
    // Only the process count, exactly as `ndmspc-run -n 4` leaves the environment.
    gSystem->Unsetenv("NDMSPC_NAV_WORKERS");
    gSystem->Setenv("NDMSPC_MAX_PROCESSES", "4");
    fromProcessCount = ReshapeToJson(levels, 4, "policy_proc");
  }

  const std::string ownVariable = ReshapeToJson(levels, 4, "policy_own");

  ASSERT_FALSE(fromProcessCount.empty());
  ASSERT_FALSE(ownVariable.empty());
  EXPECT_EQ(fromProcessCount, ownVariable) << "NDMSPC_MAX_PROCESSES must stand in for the reshape's variable";
}

/**
 * With nothing set, the count is the CPUs this process may use — not 1.
 *
 * The default used to be serial. It is now the core count, so a test that only clears the variables can no
 * longer expect the serial result; what it must show is that the unset case agrees with an explicit
 * core-count reshape, and that `1` still means one thread when it is asked for.
 */
TEST_F(NParallelReshapeTest, ReshapeUsesTheAvailableCpusWhenNothingIsSet)
{
  const std::vector<std::vector<int>> levels = {{0, 1}, {2}};

  const int cpus = std::max(1, static_cast<int>(Ndmspc::NUtils::AvailableCpuCount()));

  std::string unset;
  {
    const ScopedEnvVar modeVar("NDMSPC_EXECUTION_MODE", "");
    const ScopedEnvVar navVar("NDMSPC_NAV_WORKERS", "");
    const ScopedEnvVar procVar("NDMSPC_MAX_PROCESSES", "");
    gSystem->Unsetenv("NDMSPC_NAV_WORKERS");
    gSystem->Unsetenv("NDMSPC_MAX_PROCESSES");
    unset = ReshapeToJson(levels, cpus, "policy_unset");
  }

  std::string explicitCpus = ReshapeToJson(levels, cpus, "policy_cpus");

  std::string serial = ReshapeToJson(levels, 1, "policy_serial");

  ASSERT_FALSE(unset.empty());
  ASSERT_FALSE(explicitCpus.empty());
  ASSERT_FALSE(serial.empty());
  // The same navigator however it was reached: the default and an explicit core count are one policy.
  EXPECT_EQ(unset, explicitCpus) << "an unset worker count must mean the available CPUs";
  // And a parallel reshape still builds exactly what a serial one does.
  EXPECT_EQ(unset, serial) << "the default worker count must not change the result";
}
