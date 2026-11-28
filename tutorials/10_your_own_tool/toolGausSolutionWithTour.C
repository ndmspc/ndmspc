/// toolGausSolutionWithTour.C — the worked tool, plus a guided tour.
///
/// The same two steps as toolGausSolution.C (generate → draw), with a tour declared on the first
/// one: the room offers it as "Make a Gaussian and draw it", and walking it configures the tool and
/// runs both steps for the user.
///
///   cd ~/ndmspc-tutorial/20261119-alice
///   ndmspc-server --no-default-tools -m 05_your_own_tool/toolGausSolutionWithTour.C
///
/// Open http://localhost:8080, and use Help (the ? beside the room's title) to start the tour.

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



json GausTour()
{
  json steps = json::array();

  steps.push_back(json{{"show", "group"},
                       {"body", "This is the tool's group: one action to generate a histogram, and "
                                "one to draw it."}});
  steps.push_back(json{{"show", "stepBlock"},
                       {"action", "myname/gaus/generate"},
                       {"params", {{"entries", 10000}, {"mean", 0.0}, {"sigma", 1.0}}},
                       {"body", "The first step: what to generate. Its settings are its own, so a "
                                "second Gaussian is a second session."}});
  steps.push_back(json{{"show", "field:entries"},
                       {"body", "How many random numbers to draw. Ten thousand is enough for a "
                                "smooth histogram."}});
  steps.push_back(json{{"show", "field:mean"}, {"body", "The mean the numbers are drawn around."}});
  steps.push_back(json{{"show", "field:sigma"},
                       {"body", "And their width. Change any of these and the step you run is a "
                                "different one."}});
  steps.push_back(json{{"show", "field:pad"}, {"body", "The pad the log goes to. Leave it at pad1."}});
  steps.push_back(json{{"show", "saveAndRun"},
                       {"body", "This runs the step: the histogram is filled and fitted, and its "
                                "parameters are written to the pad below as markdown. Press Next when "
                                "it has run."}});
  steps.push_back(json{{"show", "pads"},
                       {"body", "What came back: the settings on the left, the fitted mean and sigma "
                                "on the right — compare them with what you asked for. What it made is "
                                "a real ROOT histogram, kept for the second step."}});
  steps.push_back(json{{"show", "stepBlock"},
                       {"action", "myname/gaus/draw"},
                       {"params", {{"pad", "pad2"}}},
                       {"body", "The second step hangs under the first, and it exists because of "
                                "it: draw asks generate's histogram for the picture."}});
  steps.push_back(json{{"show", "field:pad"},
                       {"body", "Which pad to draw into. pad2 by default, so the log and the picture "
                                "sit side by side."}});
  steps.push_back(
      json{{"show", "saveAndRun"}, {"body", "Run it — the histogram is drawn, with the fit on top."}});
  steps.push_back(json{{"show", "pads"},
                       {"body", "The Gaussian, fitted. That is the whole tool: two steps, a form for "
                                "each, and the result in the pads."}});

  return json{{"name", "Make a Gaussian and draw it"},
              {"description",
               "Generate a Gaussian histogram with the parameters you choose, look at the fit in a "
               "log, then draw the histogram itself."},
              {"steps", steps}};
}

void toolGausSolutionWithTour()
{
  auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);
  const json tour = GausTour();

  // --- Step 1: generate -------------------------------------------------------------------
  // The tour is declared here, on the step a user starts from: the client offers it where Help is,
  // and walking it opens this step, then the draw step below.
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
      .tutorial    = tour,
  });

  // --- Step 2: draw -----------------------------------------------------------------------
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

        const int         entries = Ndmspc::Number<int>(in, "entries", 10000);
        const double      mean    = Ndmspc::Number<double>(in, "mean", 0.0);
        const double      sigma   = Ndmspc::Number<double>(in, "sigma", 1.0);
        const std::string pad     = in.value("pad", std::string("pad1"));

        // Kept per session, under the name this request's session gives it, so the draw step
        // (another request) finds it again.
        TH1D *h = Gaus(entries, mean, sigma);
        if (auto *server = ctx.Server()) server->AddInputObject(ctx.ObjectName("hGaus"), h);

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
