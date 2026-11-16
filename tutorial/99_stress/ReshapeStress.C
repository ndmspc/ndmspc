#include <chrono>
#include <cstdio>
#include <string>

#include <TROOT.h>
#include <TSystem.h>

#include <ndmspc/core/NGnNavigator.h>
#include <ndmspc/core/NGnTree.h>

static double NowSec()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Reshape one NGnTree and report how long it took, with whatever worker settings it was launched under.
void ReshapeStress(std::string file = "stress.root")
{
  const char * workers = gSystem->Getenv("NDMSPC_NAV_WORKERS");
  const char * mode    = gSystem->Getenv("NDMSPC_EXECUTION_MODE");

  printf("RS file=%s mode=%s navWorkers=%s implicitMT=%d\n", file.c_str(), mode ? mode : "(unset)",
         workers ? workers : "(unset)", (int)ROOT::IsImplicitMTEnabled());

  Ndmspc::NGnTree * ngnt = Ndmspc::NGnTree::Open(file);
  if (!ngnt || ngnt->IsZombie()) {
    printf("RS cannot open %s\n", file.c_str());
    return;
  }
  printf("RS points=%lld\n", (long long)ngnt->GetEntries());

  // Reshape requires the levels to cover every variable axis, so a fixed pair of levels only fits one tree
  // shape. Split the axes in two layers, half and half: a first layer wide enough to have several bins for
  // workers to take, which is the thing being measured.
  const size_t nAxes = ngnt->GetBinning()->GetAxes().size();
  if (nAxes < 2) {
    printf("RS needs at least 2 axes to split into levels (this tree has %zu)\n", nAxes);
    ngnt->Close();
    delete ngnt;
    return;
  }
  const size_t                         firstLayer = std::max<size_t>(1, nAxes / 2);
  std::vector<std::vector<int>>        levels(2);
  for (size_t i = 0; i < nAxes; i++) levels[i < firstLayer ? 0 : 1].push_back((int)i);
  printf("RS levels=%s\n", json(levels).dump().c_str());

  const double started = NowSec();
  auto *       nav     = ngnt->Reshape(ngnt->GetBinning()->GetCurrentDefinitionName(), levels, 0, {}, {});
  const double elapsed = NowSec() - started;

  printf("RS reshape=%.3f s nav=%s\n", elapsed, nav ? "OK" : "NULL");
  if (nav != nullptr) {
    printf("RS children=%zu nLevels=%zu\n", nav->GetChildren().size(), nav->GetNLevels());
  }

  delete nav;
  ngnt->Close();
  delete ngnt;
}
