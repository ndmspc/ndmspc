#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include <TSystem.h>

#include "ndmspc/core/NGnTree.h"
#include "ndmspc/core/NUtils.h"
#include "ndmspc/core/NStress.h"

namespace {

/**
 * `NStress` builds a grid of copies and measures reshape on it.
 *
 * The layout is the load-bearing part: the copies have to be real files (a link would put a cached
 * navigator in every bin at once, making the measurement meaningless), and the axis names come from the
 * copy tree rather than the directories, which carry only bin labels.
 */
class NStressTest : public ::testing::Test {
  protected:
  static const std::string workdir;

  void SetUp() override
  {
    gSystem->Exec(("rm -rf " + workdir).c_str());
    gSystem->mkdir(workdir.c_str(), kTRUE);
  }
  void TearDown() override { gSystem->Exec(("rm -rf " + workdir).c_str()); }

  /// A small, prepared stress object: a 3-bin synthetic source over a 2x2 grid.
  static Ndmspc::NStress Small(const std::string & axes = "a1=2,a2=2")
  {
    Ndmspc::NStress stress;
    stress.SetWorkdir(workdir);
    stress.SetAxes(axes);
    stress.SetGenerate(3, 300);
    return stress;
  }
};

const std::string NStressTest::workdir = "test_NStress_class";

} // namespace

/**
 * `:` separates axes, `,` separates labels, and a bare number is a count.
 *
 * The two-separator form is what makes a mixed specification readable: `a1=x,y:a2=3` cannot be misread,
 * while the legacy single-comma form had to be disambiguated by looking for `=`.
 */
TEST_F(NStressTest, AxisSpecsTakeColonAndComma)
{
  // Labels and counts mixed, the shape the CLI documents.
  Ndmspc::NStress stress = Small("a1=label1,label2:a2=2:a3=alpha,beta:a4=2");
  // ParseAxes is private, so this is checked where it shows: the grid it builds.
  ASSERT_TRUE(stress.Build()) << "the mixed axis specification was rejected";

  // a1 has 2 labels, a2 has 2 numbered bins, a3 has 2 labels, a4 has 2 numbered bins.
  const std::string leaf = workdir + "/label2/2/beta/2/d.root";
  EXPECT_FALSE(gSystem->AccessPathName(leaf.c_str()))
      << "the layout does not match the labels: expected " << leaf;
  EXPECT_FALSE(gSystem->AccessPathName((workdir + "/label1/1/alpha/1/d.root").c_str()));
  EXPECT_TRUE(stress.Verify());

  // The legacy form still works, since every existing command uses it.
  Ndmspc::NStress legacy;
  legacy.SetWorkdir(workdir + "/legacy");
  legacy.SetAxes("a1=2,a2=2");
  legacy.SetGenerate(3, 200);
  EXPECT_TRUE(legacy.Build()) << "the legacy comma form stopped working";
}

/** Building produces a real grid of separate files, and Verify confirms it. */
TEST_F(NStressTest, BuildMakesSeparateCopies)
{
  Ndmspc::NStress stress = Small();
  ASSERT_TRUE(stress.Build());

  const std::string first  = workdir + "/1/1/d.root";
  const std::string second = workdir + "/1/2/d.root";
  ASSERT_FALSE(gSystem->AccessPathName(first.c_str())) << first;
  ASSERT_FALSE(gSystem->AccessPathName(second.c_str())) << second;

  FileStat_t a, b;
  ASSERT_EQ(gSystem->GetPathInfo(first.c_str(), a), 0);
  ASSERT_EQ(gSystem->GetPathInfo(second.c_str(), b), 0);
  // Same device but a different inode is the property the measurement depends on.
  EXPECT_FALSE(a.fDev == b.fDev && a.fIno == b.fIno) << "the copies share an inode";
  EXPECT_EQ(a.fSize, b.fSize) << "both came from one source, so they must be the same size";

  EXPECT_TRUE(stress.Verify());
}

/** Verify rejects a layout whose bins are one file behind two names. */
TEST_F(NStressTest, VerifyRejectsLinkedCopies)
{
  Ndmspc::NStress stress = Small();
  ASSERT_TRUE(stress.Build());

  // Replace one copy with a hardlink to another: exactly the mistake that would make the measurement
  // meaningless, and the thing Verify exists to catch. Made with the shell so the link is known to exist
  // before Verify is asked about it — a silent failure here would pass the test for the wrong reason.
  const std::string first  = workdir + "/1/1/d.root";
  const std::string second = workdir + "/1/2/d.root";
  ASSERT_EQ(gSystem->Unlink(second.c_str()), 0);
  ASSERT_EQ(gSystem->Exec(("ln " + first + " " + second).c_str()), 0) << "the hardlink could not be made";

  FileStat_t a, b;
  ASSERT_EQ(gSystem->GetPathInfo(first.c_str(), a), 0);
  ASSERT_EQ(gSystem->GetPathInfo(second.c_str(), b), 0);
  ASSERT_TRUE(a.fDev == b.fDev && a.fIno == b.fIno) << "the test did not manage to make one file behind two names";

  EXPECT_FALSE(stress.Verify()) << "verify accepted two names for one file";
}

/** The runs become one ordinary NGnTree: a point per phase, with the numbers on it. */
TEST_F(NStressTest, RunsBecomeOneTreeWithAPointPerPhase)
{
  Ndmspc::NStress stress = Small();
  ASSERT_TRUE(stress.Build());
  ASSERT_TRUE(stress.Verify());

  // `Run` reports whether the phases **ran**, not whether the cache won: on a 36-point tree the reshape is a
  // couple of milliseconds and timer noise can outweigh the saving. A real speed-up is a property of a tree
  // big enough to have one — the `nParamCopies` trees in the docs measure that.
  ASSERT_TRUE(stress.Run("local")) << "a phase did not reshape at all";
  EXPECT_GT(stress.GetCold(), 0.0);
  EXPECT_GT(stress.GetWarm(), 0.0);

  const std::string file    = workdir + "/results.root";
  Ndmspc::NGnTree * results = stress.GetResults(file);
  ASSERT_NE(results, nullptr);

  const auto axes = results->GetBinning()->GetAxes();
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(std::string(axes[0]->GetName()), "run");
  EXPECT_EQ(std::string(axes[1]->GetName()), "phase");
  EXPECT_EQ(axes[1]->GetNbins(), 3) << "cold cache-off, cold cache-on and warm cache-hit";
  delete results;

  // Reopened from what was written, which is what a second reader would see.
  Ndmspc::NGnTree * again = Ndmspc::NGnTree::Open(file);
  ASSERT_NE(again, nullptr);
  auto * definition = again->GetBinning()->GetDefinition("default");
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(definition->GetIds().size(), 3u) << "one run x three phases";
  delete again;
}

/** A second run lands in the same tree, so two storages can be compared in one place. */
TEST_F(NStressTest, ASecRunAddsAPointPerPhase)
{
  Ndmspc::NStress stress = Small();
  ASSERT_TRUE(stress.Build());
  ASSERT_TRUE(stress.Run("local"));

  // A second workdir standing in for another storage: the same measurement, elsewhere.
  stress.SetWorkdir(workdir + "/eos");
  stress.SetForce(true);
  ASSERT_TRUE(stress.Build());
  ASSERT_TRUE(stress.Run("eos"));

  EXPECT_EQ(stress.GetRuns().size(), 2u);

  const std::string file    = workdir + "/results.root";
  Ndmspc::NGnTree * results = stress.GetResults(file);
  ASSERT_NE(results, nullptr);
  delete results;

  Ndmspc::NGnTree * again = Ndmspc::NGnTree::Open(file);
  ASSERT_NE(again, nullptr);
  auto * definition = again->GetBinning()->GetDefinition("default");
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(definition->GetIds().size(), 6u) << "two runs x three phases, in one tree";
  EXPECT_EQ(again->GetBinning()->GetAxes()[0]->GetNbins(), 2) << "the run axis holds both runs";
  delete again;
}

/** The derived levels cover every axis, which is what Reshape requires. */
TEST_F(NStressTest, RunDerivesLevelsCoveringEveryAxis)
{
  Ndmspc::NStress stress = Small();
  ASSERT_TRUE(stress.Build());
  ASSERT_TRUE(stress.Run("derived")) << "the derived levels were not accepted";

  const auto levels  = json::parse(stress.GetResolvedLevels()).get<std::vector<std::vector<int>>>();
  size_t     covered = 0;
  for (const auto & layer : levels) covered += layer.size();
  // The grid's two axes plus the source tree's two.
  EXPECT_EQ(covered, 4u) << "levels must cover every variable axis: " << stress.GetResolvedLevels();
}

/**
 * `NUtils::Rm`, which is what makes `--force` a real rebuild.
 *
 * One call for both worlds: a local tree is walked and unlinked, a remote one goes through the storage
 * element's own command interface. The local half is tested here; the remote half needs a reachable EOS,
 * so it is exercised by hand rather than in the suite.
 */
TEST(NUtilsRmTest, RemovesALocalTreeAndASingleFile)
{
  gSystem->Exec("rm -rf /tmp/test_NUtilsRm");
  gSystem->mkdir("/tmp/test_NUtilsRm/a/b/c", kTRUE);
  gSystem->Exec("touch /tmp/test_NUtilsRm/a/b/c/one.root /tmp/test_NUtilsRm/a/b/two.root");

  ASSERT_NE(gSystem->OpenDirectory("/tmp/test_NUtilsRm/a"), nullptr) << "the tree was not created";
  EXPECT_TRUE(Ndmspc::NUtils::Rm("/tmp/test_NUtilsRm", true));
  EXPECT_EQ(gSystem->OpenDirectory("/tmp/test_NUtilsRm"), nullptr) << "the tree survived";

  // A single file needs no recursion.
  gSystem->Exec("touch /tmp/test_NUtilsRm_single.root");
  EXPECT_TRUE(Ndmspc::NUtils::Rm("/tmp/test_NUtilsRm_single.root"));
  EXPECT_FALSE(gSystem->AccessPathName("/tmp/test_NUtilsRm_single.root") == 0);

  // Removing what is already gone succeeds: a cleanup step must be safe to repeat.
  EXPECT_TRUE(Ndmspc::NUtils::Rm("/tmp/test_NUtilsRm"));
}

/** A non-empty directory is refused unless recursion is asked for, rather than half-emptied. */
TEST(NUtilsRmTest, RefusesANonEmptyDirectoryWithoutRecursion)
{
  gSystem->Exec("rm -rf /tmp/test_NUtilsRm2");
  gSystem->mkdir("/tmp/test_NUtilsRm2/a", kTRUE);
  gSystem->Exec("touch /tmp/test_NUtilsRm2/a/keep.root");

  EXPECT_FALSE(Ndmspc::NUtils::Rm("/tmp/test_NUtilsRm2", false));
  EXPECT_NE(gSystem->OpenDirectory("/tmp/test_NUtilsRm2/a"), nullptr) << "the directory was emptied anyway";

  EXPECT_TRUE(Ndmspc::NUtils::Rm("/tmp/test_NUtilsRm2", true));
}
