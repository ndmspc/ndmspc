/**
 * Tests for NRouteContext's "show something in a pad" helpers: the `payload.pad` envelope the
 * viewport reads, the ROOT-object shortcut, and the click-action builder.
 */

#include <gtest/gtest.h>
#include <map>
#include <string>
#include <TH1D.h>
#include <TObject.h>
#include "ndmspc/http/NRouteContext.h"

using namespace Ndmspc;

namespace {

/// A context over fresh JSON payloads, the way a handler gets one.
struct Ctx {
  json                             in  = json::object();
  json                             out = json::object();
  json                             ws  = json::object();
  std::map<std::string, TObject *> objects;
  NRouteContext                    ctx{"POST", in, out, ws, objects};
};

/// The envelopes written so far (`payload.pad`), or an empty array when there are none.
json pads(const Ctx & c)
{
  if (!c.ws.contains("payload") || !c.ws["payload"].contains("pad")) return json::array();
  return c.ws["payload"]["pad"];
}

} // namespace

TEST(NRouteContextShow, AppendsOneEnvelopePerCall)
{
  Ctx c;
  c.ctx.Show(json{{"lines", json::array({"one", "two"})}}, "log", "pad2", "Log");
  c.ctx.Show("hello", "markdown", "pad1", "Notes");

  const json written = pads(c);
  ASSERT_TRUE(written.is_array());
  ASSERT_EQ(written.size(), 2u);

  EXPECT_EQ(written[0]["pad"], "pad2");
  EXPECT_EQ(written[0]["kind"], "log");
  EXPECT_EQ(written[0]["label"], "Log");
  EXPECT_EQ(written[0]["value"]["lines"].size(), 2u);

  EXPECT_EQ(written[1]["pad"], "pad1");
  EXPECT_EQ(written[1]["kind"], "markdown");
  EXPECT_EQ(written[1]["value"], "hello");
}

TEST(NRouteContextShow, CarriesOptionsAndHandlers)
{
  Ctx  c;
  json click = json::array({NRouteContext::Action("ndmspc/ngnt/map", "PATCH", json{{"level", 1}})});
  c.ctx.Show(json{{"_typename", "TH1D"}}, "jsroot", "pad1", "h1", json{{"drawOpts", "colz"}},
             json{{"click", click}});

  const json written = pads(c);
  ASSERT_EQ(written.size(), 1u);
  EXPECT_EQ(written[0]["options"]["drawOpts"], "colz");
  EXPECT_EQ(written[0]["handlers"]["click"][0]["type"], "http");
  EXPECT_EQ(written[0]["handlers"]["click"][0]["method"], "PATCH");
  EXPECT_EQ(written[0]["handlers"]["click"][0]["path"], "ndmspc/ngnt/map");
  EXPECT_EQ(written[0]["handlers"]["click"][0]["contentType"], "application/json");
  EXPECT_EQ(written[0]["handlers"]["click"][0]["payload"]["level"], 1);
}

TEST(NRouteContextShow, DefaultsToTheFirstPadAndTheJsonKind)
{
  Ctx c;
  c.ctx.Show(json::array({1, 2, 3}));

  const json written = pads(c);
  ASSERT_EQ(written.size(), 1u);
  EXPECT_EQ(written[0]["pad"], "pad1");
  EXPECT_EQ(written[0]["kind"], "json");
  EXPECT_EQ(written[0]["value"].size(), 3u);
  // Nothing was asked for, so nothing is claimed about it.
  EXPECT_FALSE(written[0].contains("label"));
  EXPECT_FALSE(written[0].contains("options"));
  EXPECT_FALSE(written[0].contains("handlers"));
}

TEST(NRouteContextShow, ActionIsTheShapeTheUiCarriesOut)
{
  const json action = NRouteContext::Action("ndmspc/ngnt/spectra", "GET", json{{"point", json::array({1})}},
                                            "application/json");
  EXPECT_EQ(action["type"], "http");
  EXPECT_EQ(action["method"], "GET");
  EXPECT_EQ(action["path"], "ndmspc/ngnt/spectra");
  EXPECT_EQ(action["payload"]["point"][0], 1);
}

TEST(NRouteContextShow, ShowRootPacksAJsrootObject)
{
  TH1D histogram("h", "h;x;y", 4, 0.0, 1.0);
  histogram.Fill(0.5);

  Ctx c;
  c.ctx.ShowRoot(&histogram, "pad3", "", "colz");

  const json written = pads(c);
  ASSERT_EQ(written.size(), 1u);
  EXPECT_EQ(written[0]["pad"], "pad3");
  EXPECT_EQ(written[0]["kind"], "jsroot");
  // Named after the object when no tab was asked for.
  EXPECT_EQ(written[0]["label"], "h");
  EXPECT_EQ(written[0]["options"]["drawOpts"], "colz");
  EXPECT_EQ(written[0]["value"]["_typename"], "TH1D");
  EXPECT_EQ(written[0]["value"]["fName"], "h");
  EXPECT_FALSE(c.ctx.HasError());
}

TEST(NRouteContextShow, ShowRootWithoutAnObjectIsAnError)
{
  Ctx c;
  c.ctx.ShowRoot(nullptr);
  EXPECT_TRUE(c.ctx.HasError());
  EXPECT_EQ(pads(c).size(), 0u);
}

TEST(NRouteContextShow, KeepsAnEnvelopeAHandlerWroteItself)
{
  Ctx c;
  c.ws["payload"]["pad"] = json{{"pad", "pad9"}, {"kind", "log"}, {"value", json::array()}};
  c.ctx.Show("second", "markdown", "pad2", "Notes");

  const json written = pads(c);
  ASSERT_EQ(written.size(), 2u);
  EXPECT_EQ(written[0]["pad"], "pad9");
  EXPECT_EQ(written[1]["pad"], "pad2");
}
