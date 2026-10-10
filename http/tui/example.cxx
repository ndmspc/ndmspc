// The worked example for this directory: the smallest thing that uses FTXUI, so the TUI dependency
// (core/tui's NdmspcTuiFtxui, and FTXUI itself) stays exercised and a real TUI has something to
// copy.
//
// It is not built by default - http/CMakeLists.txt adds this directory only with -DWITH_TUI=ON (or
// `scripts/make.sh tui`). FTXUI's implementation is compiled once, in core/tui/ftxui_impl.cxx; a
// TUI must never define FTXUI_IMPLEMENTATION itself, only include this header and link the target.
#include <string>
#include <vector>

#include "ftxui_all.hpp"

int main()
{
  using namespace ftxui;

  std::vector<std::string> entries  = {"alpha", "beta", "gamma"};
  int                      selected = 0;

  auto screen = ScreenInteractive::Fullscreen();
  auto menu   = Menu(&entries, &selected);
  auto root   = Renderer(menu, [&] {
    return vbox({
               text("ndmspc TUI example") | bold,
               separator(),
               menu->Render(),
               separator(),
               text("selected: " + entries[selected]) | dim,
           }) |
           border;
  });
  // `q` or Esc quits, so the example is not a terminal trap.
  auto app = CatchEvent(root, [&](const Event & event) {
    if (event == Event::Character('q') || event == Event::Escape) {
      screen.Exit();
      return true;
    }
    return false;
  });

  screen.Loop(app);
  return 0;
}
