# Terminal UIs for the ndmspc HTTP services.
#
# The FTXUI implementation is compiled once in core/tui (target NdmspcTuiFtxui), which
# also carries FTXUI's include directories and the thread dependency, so a TUI here
# only needs to link it. Neither this executable's sources nor core/tui include a ROOT
# header - ROOT arrives by linking NdmspcHttp.
#
# This is a directory of its own rather than another entry under cli/ because
# http/cli/CMakeLists.txt turns every *.cxx into its own executable, which would also
# sweep up anything else added here.

include_directories(${CLI11_INCLUDE_DIRS})

# room_mgm_ui.cxx holds the FTXUI screen; ndmspc-room-tui.cxx is the CLI, the
# headless actions and the wiring, and includes no FTXUI header itself.
set(MY_SOURCES
  ndmspc-room-tui.cxx
  room_mgm_ui.cxx
)

set(MY_EXTERNAL_LIBS
  NdmspcHttp
  NdmspcTuiFtxui
)

# RootBin's DEPLIBS must be a CMake *list*. A space-separated string arrives as a
# single library name and the link fails with "-lNdmspcHttp NdmspcTuiFtxui".
RootBin(ndmspc-room-tui "${MY_SOURCES}" "${MY_EXTERNAL_LIBS}")
