/// toolGausSolution.C — the worked version of toolGaus.C.
///
/// Two steps, one under the other:
///
///   generate  — fills and fits the histogram; writes the settings and the fitted parameters as
///               markdown to the pad it is told (pad1 by default).
///   draw      — draws the histogram generate made into the pad it is told (pad2 by default).
///
///   cd ~/ndmspc-tutorial/20261119-alice
///   ndmspc-server --no-default-tools -m 05_your_own_tool/toolGausSolution.C
///
/// Open http://localhost:8080, pick the myname/gaus tool in the Explorer, set entries, mean and
/// sigma, press Save & Run, then run the DRAW step.

#include <map>
#include <string>

#include <TFitResult.h>
#include <TH1D.h>
#include <TObject.h>
#include <TRandom.h>

#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NInstanceTree.h>
#include <ndmspc/http/NRouteContext.h>

/// Fill a histogram with `nEntries` Gaussian random numbers of the given `mean` and `sigma` and fit
/// it. The fit stays on the histogram as its "gaus" function, so a caller reads the parameters from
/// there rather than passing a TFitResultPtr around.
TH1D *Gaus(int nEntries = 10000, double mean = 0.0, double sigma = 1.0)
{
  TH1D *h = new TH1D("hGaus", "Gaussian", 200, -10, 10);

  for (int i = 0; i < nEntries; i++) {
    h->Fill(gRandom->Gaus(mean, sigma));
  }

  h->Fit("gaus", "QS");

  return h;
}



void toolGausSolution()
{
  auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // --- Step 1: generate -------------------------------------------------------------------
  // The step that starts the combination: what a user configures. It needs no `root` flag — the
  // `draw` step below declares `dependsOn`, which is what makes this group a combination and this
  // action its first step. That also means it runs on v1.4.18, where `root` does not exist yet.
  Ndmspc::RegisterMcpTool("myname/gaus/generate", {
      .description = "Fill a Gaussian histogram and fit it; the parameters go to a pad as markdown.",
      .methods     = {"POST"},
      // `order` is what the form reads: a JSON object's keys come sorted, which would put the log pad
      // between mean and sigma. Naming them is how the form says entries, mean, sigma, then the pad.
      .inputSchema = {{"order", {"entries", "mean", "sigma", "pad"}},
                      {"properties",
                       {{"entries", {{"type", "integer"}, {"title", "Entries"}, {"default", 10000}}},
                        {"mean", {{"type", "number"}, {"title", "Mean"}, {"default", 0.0}}},
                        {"sigma", {{"type", "number"}, {"title", "Sigma"}, {"default", 1.0}}},
                        {"pad", {{"type", "string"}, {"title", "Log pad"}, {"default", "pad1"}}}}}},
      .label       = "N {{ entries }}, μ {{ mean }}, σ {{ sigma }}",
  });

  // --- Step 2: draw -----------------------------------------------------------------------
  // The step under it: draws what generate made. Its settings are its own, so several draws of
  // the same histogram can live on different pads.
  Ndmspc::RegisterMcpTool("myname/gaus/draw", {
      .description = "Draw the histogram the generate step made.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"pad", {{"type", "string"}, {"title", "Pad"}, {"default", "pad2"}}}}}},
      .dependsOn   = {"myname/gaus/generate"},
  });

  handlers["myname/gaus/generate"] =
      [](std::string method, json &in, json &out, json &wsOut,
         std::map<std::string, TObject *> &objects) {
        Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);

        const int    entries = Ndmspc::Number<int>(in, "entries", 10000);
        const double mean    = Ndmspc::Number<double>(in, "mean", 0.0);
        const double sigma   = Ndmspc::Number<double>(in, "sigma", 1.0);
        const std::string pad = in.value("pad", std::string("pad1"));

        // The histogram is kept per session, under the name this request's session gives it, so
        // the draw step (another request, maybe another user) finds it again.
        TH1D *h = Gaus(entries, mean, sigma);
        if (auto *server = ctx.Server()) server->AddInputObject(ctx.ObjectName("hGaus"), h);

        // What was asked and what came back, as markdown — the step's own log.
        // The settings read as a sentence — they are what the user typed — and the fit comes back as a
        // table, because the fitted values and their errors are numbers meant to be compared column by
        // column. A blank line between them is what makes the table start its own block in markdown.
        const TF1 *fit = h->GetFunction("gaus");
        std::string text =
            TString::Format("## Gaussian\n\nGenerated **%d** entries with mean **%.2f** and sigma "
                            "**%.2f**.\n\n",
                            entries, mean, sigma)
                .Data();
        if (fit != nullptr) {
          text += "| fitted | value | error |\n| --- | --- | --- |\n";
          text += TString::Format("| mean | %.2f | %.2f |\n", fit->GetParameter(1), fit->GetParError(1))
                      .Data();
          text += TString::Format("| sigma | %.2f | %.2f |\n", fit->GetParameter(2),
                                  fit->GetParError(2))
                      .Data();
        }

        ctx.Show(text, "markdown", pad);
        ctx.Success();
      };

  handlers["myname/gaus/draw"] =
      [](std::string method, json &in, json &out, json &wsOut,
         std::map<std::string, TObject *> &objects) {
        Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);

        const json        parent = Ndmspc::ParentParams(ctx);
        const std::string pad    = in.value("pad", std::string("pad2"));

        TH1D *h = ctx.GetObject<TH1D>(ctx.ObjectName("hGaus"));
        if (h == nullptr) {
          ctx.Error("nothing to draw: run the generate step first");
          return;
        }

        h->SetTitle(TString::Format("Gaussian: N=%d, #mu=%.2f, #sigma=%.2f",
                                    Ndmspc::Number<int>(parent, "entries", 10000),
                                    Ndmspc::Number<double>(parent, "mean", 0.0),
                                    Ndmspc::Number<double>(parent, "sigma", 1.0))
                        .Data());
        ctx.ShowRoot(h, pad, "histogram");
        ctx.Success();
      };
}
