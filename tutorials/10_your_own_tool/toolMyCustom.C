/// toolMyCustom.C — the same tool as one action, on ndmspc v1.4.21 or newer (the `root` flag).
///
/// `root = true` says the action starts a combination even though nothing depends on it, so the
/// Explorer offers its form as the first step and running it creates the session. On v1.4.18 this
/// macro does not load (the field does not exist there): use toolMyCustomNoRoot.C.
/// v1.4.21 is the release that added `root` to NMcpToolInfo.
///
/// Run it alone, from the tutorial directory:
///
///   cd ~/ndmspc-tutorial/20261119-alice
///   ndmspc-server --no-default-tools -m 05_your_own_tool/toolMyCustom.C
///
/// Then open http://localhost:8080, type a name in the Explorer's START form
/// and press Save & Run: pad1 gets the markdown Hello <name>.

#include <map>
#include <string>

#include <TObject.h>

#include <ndmspc/http/NHttpServer.h>
#include <ndmspc/http/NRouteContext.h>

void toolMyCustom()
{
  auto &handlers = *(Ndmspc::gNdmspcHttpHandlers);

  Ndmspc::RegisterMcpTool("hello/world/say", {
      .description = "Print Hello <name> as markdown.",
      .methods     = {"POST"},
      .inputSchema = {{"properties",
                       {{"name",
                         {{"type", "string"}, {"title", "Name"}, {"default", "World"}}}}}},
      .root        = true,
  });

  handlers["hello/world/say"] =
      [](std::string method, json &in, json &out, json &wsOut,
         std::map<std::string, TObject *> &objects) {
        Ndmspc::NRouteContext ctx(method, in, out, wsOut, objects);
        ctx.Show("# Hello " + in.value("name", "World"), "markdown", "pad1");
        ctx.Success();
      };
}
