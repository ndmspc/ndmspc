#include <gtest/gtest.h>

#include <TCanvas.h>
#include <TF1.h>
#include <TFitResult.h>
#include <TH1D.h>
#include <TList.h>
#include <TPad.h>
#include <TVirtualPad.h>

#include <vector>

#include "ndmspc/core/NGnThreadData.h"

namespace {

/**
 * A worker's deferred-delete drain must not leave the thread's pad pointing at freed objects.
 *
 * `h->Fit()` draws into the thread's `gPad`, which puts the histogram in that pad's list of primitives.
 * The pad is made once per thread and outlives the bin, while the histogram is freed at the end of it.
 * The next `TH1::Fit` walks the pad's list while looking up the function it is about to add, so a
 * dangling entry there is dereferenced and the worker dies part-way through a fit-heavy run — which is
 * how forked IPC workers aborted under `NCpuStress.C` and `NParameter01Gaus.C`.
 *
 * The regression this locks is the second fit: with the pad still pointing at the first bin's freed
 * histogram it crashed; with the drain detaching what it frees, it does not.
 */
TEST(NGnThreadDataDrain, SecondFitSurvivesTheFirstBinsDrain)
{
  TCanvas * canvas = new TCanvas("drainTestPad", "drainTestPad", 1, 1);
  canvas->SetBit(kCanDelete, kFALSE);
  TPad * pad = dynamic_cast<TPad *>(canvas->cd());
  ASSERT_NE(pad, nullptr);

  Ndmspc::NGnThreadData worker;

  auto oneBin = [&](int bin) {
    TH1D * h = new TH1D(Form("hDrain%d", bin), "h", 100, -10, 10);
    h->FillRandom("gaus", 2000);
    TF1 * f = new TF1("gausFunc", "gaus", -10, 10);
    f->AddToGlobalList(false);

    TFitResultPtr result = h->Fit(f, "QS");
    EXPECT_GT(result->Parameter(1), -5.0);

    pad->Add(h); // what drawing the fit leaves behind
    std::vector<TObject *> drawn{h};
    worker.DeferDeletes(drawn);
    worker.FlushDeferredDeletes();

    EXPECT_EQ(pad->GetListOfPrimitives()->FindObject(h), nullptr)
        << "the drained histogram is still a primitive of the pad after bin " << bin;
  };

  oneBin(0);
  oneBin(1); // this is the fit that used to dereference the freed histogram

  delete canvas;
}

} // namespace
