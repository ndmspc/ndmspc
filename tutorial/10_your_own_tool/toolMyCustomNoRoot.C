/// toolMyCustomNoRoot.C — the "writing your own tool" exercise on ndmspc v1.4.18.
///
/// v1.4.18 has no `root` flag, so a single action cannot start a combination: the Explorer lists
/// only groups whose actions declare a dependency. The same tool is therefore split in two steps
/// here. On ndmspc v1.4.21 or newer (the `root` flag), use toolMyCustom.C instead.
///
/// Run it alone, from the tutorial directory:
///
///   cd ~/ndmspc-tutorial/20261119-alice
///   ndmspc-server --no-default-tools -m 05_your_own_tool/toolMyCustomNoRoot.C
///
/// Then open http://localhost:8080, open the Explorer, type a name in the START
/// step's Name field and press Save & Run: the SAY step draws Hello <name> on pad1.

#include <map>
#include <string>

#include <TObject.h>

#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NInstanceTree.h>
#include <ndmspc/http/NRouteContext.h>

void toolMyCustomNoRoot()
{
  auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);

  // The step that starts the combination: what the Explorer offers first.
  Ndmspc::RegisterMcpTool("hello/world/start", {
      .description = "Start a greeting with a name.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"name",
                         {{"type", "string"}, {"title", "Name"}, {"default", "World"}}}}}},
      .label       = "Hello {{ name }}",
  });

  // The step under it: it has nothing to ask, so all it needs is the name above.
  // POST, because a step is created by one — a GET would target a node instead of adding a step.
  Ndmspc::RegisterMcpTool("hello/world/say", {
      .description = "Print Hello <name> as markdown.",
      .methods     = {"POST"},
      .dependsOn   = {"hello/world/start"},
  });

  handlers["hello/world/start"] =
      [](std::string method, json &in, json &out, json &wsOut,
         std::map<std::string, TObject *> &objects) {
        Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
        ctx.Success();
      };

  handlers["hello/world/say"] =
      [](std::string method, json &in, json &out, json &wsOut,
         std::map<std::string, TObject *> &objects) {
        Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
        // The name is the step above's argument, not this one's: walk up to it.
        std::string name = "World";
        if (auto *server = ctx.Server()) {
          Ndmspc::NInstanceTree tree(server->GetCombinations());
          const std::string node = server->GetCurrentInstance();
          if (tree.Has(node)) {
            const json parent = tree.Params(tree.Parent(node));
            if (parent.is_object()) name = parent.value("name", name);
          }
        }
        ctx.Show("# Hello " + name, "markdown", "pad1");
        ctx.Success();
      };
}
