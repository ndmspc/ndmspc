// The only translation unit that compiles FTXUI's amalgamated (header-only)
// implementation. It deliberately includes no ROOT and no NDMSPC header, so FTXUI's
// implementation cannot collide with anything else linked into ndmspc-room-tui.
//
// FTXUI is fetched from its release archive as ftxui_all.hpp (see cmake/deps.cmake);
// nothing else in the tree may define FTXUI_IMPLEMENTATION.
#define FTXUI_IMPLEMENTATION
#include "ftxui_all.hpp"
