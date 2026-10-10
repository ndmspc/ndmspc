# The terminal UIs, built only with -DWITH_TUI=ON (http/CMakeLists.txt gates this directory).
#
# The FTXUI implementation is compiled once in core/tui (target NdmspcTuiFtxui), which also carries
# FTXUI's include directories and the thread dependency, so a TUI here only needs to link it.
#
# example.cxx is the smallest thing that uses FTXUI: the starting point for a real TUI. Neither it
# nor core/tui includes a ROOT header - ROOT would arrive by linking NdmspcHttp, as a TUI that talks
# to the server does.
set(MY_SOURCES
  example.cxx
)

set(MY_EXTERNAL_LIBS
  NdmspcTuiFtxui
)

RootBin(ndmspc-tui-example "${MY_SOURCES}" "${MY_EXTERNAL_LIBS}")
