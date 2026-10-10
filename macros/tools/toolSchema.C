/// toolSchema.C — a worked example of every schema option a tool can declare.
///
/// This macro is documentation you can run. It registers one tool, `ndmspc/schema/probe`, whose form uses
/// every field kind the UI knows how to draw, and whose handler echoes back what it was given — so a
/// tool author can read it, copy the parts they need, and see the form that the server and the UI
/// agree on.
///
/// The vocabulary is written down in `http/README.md` ("Field vocabulary"). The rule worth repeating
/// here, because it is what makes the options appear:
///
///   **The macro says what a field *is* — its `type`, `format`, `title`, `description`. The live
///   workspace says what it *can be* — `enum`, `items`, `format`, `default`.** The options only exist
///   at runtime (the binnings that have been defined, the parameters that are in the tree), so they
///   cannot be in the declaration; `tools/list` merges the two per property *and per keyword*.
///
/// Usage — load it beside the tools you already run:
///
///   ndmspc-server -m "macros/tools/toolNgnt.C,macros/tools/toolSchema.C"
///
/// Then open a room's **Explorer**: `ndmspc/schema/start` is offered as the way to begin a combination, and
/// once it has run, `ndmspc/schema/probe` is offered under it — its form is every field kind a tool may
/// declare, which is what this example is for. Both actions also appear in the **Tools** panel.
///
/// The two steps are what make the group a *combination*: `ndmspc/schema/probe` declares
/// `dependsOn = {"ndmspc/schema/start"}`, and a group with any dependency is a pipeline — its tree, its
/// inspector forms and its pads. A group with no `dependsOn` anywhere stays a set of plain tools and
/// appears only in the Tools panel.

#include <map>
#include <string>

#include <TObject.h>

#include <ndmspc/http/NHttpServer.h> ///< RegisterMcpTool / gNdmspcHttpHandlers
#include <ndmspc/http/NRouteContext.h>
#include <ndmspc/http/NSchemaBuilder.h>

namespace {

/// The live half for the root action: one field, published the same way so its form starts on a value.
json BuildStartExample()
{
  return Ndmspc::NSchemaBuilder()
      .String("label")
      .Title("Label")
      .Description("Only here to give the chain a root — nothing else to see.")
      .Default("schema example")
      .Build();
}

/// The live half of the schema: the options, and the values a form should start from.
json BuildSchemaExample()
{
  return Ndmspc::NSchemaBuilder()
      .Hint("Every field kind the UI can draw. Hover a field to read its description.")
      // A plain string.
      .String("text")
      .Title("Text")
      .Description("A plain string, typed by hand.")
      .Default("hello")
      // One out of a list. The declaration states `format: "select"` and no options; the enum arrives
      // here, with the live schema.
      .Select("choice", {"one", "two", "three"})
      .Title("Choice")
      .Description("One of three — drawn as a select.")
      .Default("two")
      // Several out of a list: `format: "multiselect"` over `items.enum`, and the default is the list
      // that starts chosen.
      .MultiSelect("choices", {"phi", "eta", "pt"})
      .Title("Choices")
      .Description("Any number of them — drawn as a multi-select.")
      .Default(json::array({"phi", "eta"}))
      // An array of arrays: the levels editor, one row per level.
      .Array("levels")
      .Title("Levels")
      .Description("One row per level, each holding its bins.")
      .Items("array")
      .ItemItems("integer")
      .Default(json::array({json::array({0}), json::array({1, 2})}))
      // An array of scalars: a list editor, one input per entry.
      .Array("bins")
      .Title("Bins")
      .Description("A flat list of integers, one input per entry.")
      .Items("integer")
      .Default(json::array({1, 2, 3}))
      // A boolean, a number, and an object (which the UI leaves as a JSON box).
      .Boolean("flag")
      .Title("Flag")
      .Description("A checkbox.")
      .Default(true)
      .Number("amount")
      .Title("Amount")
      .Description("Any number.")
      .Default(2)
      .Build();
}

} // namespace

void toolSchema()
{
  auto &      handlers = *(Ndmspc::gNdmspcHttpHandlers);
  std::string group    = "ndmspc/schema";
  /** What the family is called where a user reads it (the room's group picker, Help). */
  const char * groupLabel = "Schema";

  // ---------------------------------------------------------------------------
  //  The root of the chain. It declares no `dependsOn`, so it is what starts a
  //  combination; the step below hangs off it.
  // ---------------------------------------------------------------------------
  Ndmspc::RegisterMcpTool(group + "/start", {
      .description = "Start the schema example. A combination needs a root before anything can follow.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"label", {{"type", "string"}, {"title", "Label"}, {"description", "Only here to give the chain a root."}}}}}},
      .order       = 1,
      .label       = "{{ label }}",
  });

  // ---------------------------------------------------------------------------
  //  The declaration: what each field *is*. Options are deliberately absent
  //  here — they belong to the live schema the handler publishes.
  // ---------------------------------------------------------------------------
  Ndmspc::RegisterMcpTool(group + "/probe", {
      .description = "Echo the arguments back. A form of every field kind the UI can draw; see "
                     "macros/tools/toolSchema.C for the worked example.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"text",
                         {{"type", "string"},
                          {"title", "Text"},
                          {"description", "A plain string, typed by hand."}}},
                        {"choice",
                         {{"type", "string"},
                          {"format", "select"},
                          {"title", "Choice"},
                          {"description", "One of three — drawn as a select."}}},
                        {"choices",
                         {{"type", "array"},
                          {"format", "multiselect"},
                          {"title", "Choices"},
                          {"items", {{"type", "string"}}},
                          {"description", "Any number of them — drawn as a multi-select."}}},
                        {"levels",
                         {{"type", "array"},
                          {"title", "Levels"},
                          {"description", "One row per level, each holding its bins."},
                          {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}}}}},
                        {"bins",
                         {{"type", "array"},
                          {"title", "Bins"},
                          {"description", "A flat list of integers, one input per entry."},
                          {"items", {{"type", "integer"}}}}},
                        {"flag", {{"type", "boolean"}, {"title", "Flag"}, {"description", "A checkbox."}}},
                        {"amount", {{"type", "number"}, {"title", "Amount"}, {"description", "Any number."}}}}}},
      // The dependency is what makes this group a *combination*; it must follow `inputSchema`, since
      // designated initializers have to come in the struct's own order.
      .dependsOn   = {group + "/start"},
      .order       = 2,
  });

  // The family is named once, for every tool in it.
  for (auto & entry : *(Ndmspc::gNdmspcMcpTools)) {
    if (entry.first.rfind(group + "/", 0) == 0) entry.second.groupLabel = groupLabel;
  }

  // ---------------------------------------------------------------------------
  //  The handler: publish the live schema, then echo what arrived. A tool with
  //  nothing live to say can leave the workspace half out — its declaration is
  //  then the whole of its form.
  // ---------------------------------------------------------------------------
  handlers[group + "/start"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                  std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "ndmspc/schema";

    ctx.Workspace()["start"]    = BuildStartExample();
    wsOut["workspace"]["start"] = ctx.Workspace()["start"];

    ctx.Result("Started " + httpIn.dump());
    ctx.Success();
  };

  // ---------------------------------------------------------------------------
  //  The step that shows the fields. It publishes the live schema, then echoes
  //  what arrived. A tool with nothing live to say can leave the workspace half
  //  out — its declaration is then the whole of its form.
  // ---------------------------------------------------------------------------
  handlers[group + "/probe"] = [](std::string method, json & httpIn, json & httpOut, json & wsOut,
                                  std::map<std::string, TObject *> & objects) {
    Ndmspc::NRouteContext ctx(method, httpIn, httpOut, wsOut, objects);
    wsOut["group"] = "ndmspc/schema";

    ctx.Workspace()["probe"]    = BuildSchemaExample();
    wsOut["workspace"]["probe"] = ctx.Workspace()["probe"];

    ctx.Result("Received " + httpIn.dump());
    ctx.Success();
  };
}
