# FTXUI support for NDMSPC terminal user interfaces.
#
# FTXUI is consumed in its amalgamated header-only form: FTXUI_INCLUDE_DIRS points at
# the extracted FTXUI release holding ftxui_all.hpp. This directory compiles that
# implementation exactly once, in ftxui_impl.cxx, and hands FTXUI's include
# directories on to anything that links the result - so a TUI only has to link
# NdmspcTuiFtxui and knows nothing about how FTXUI is packaged.
#
# It lives in core/ because it is generic TUI infrastructure, not part of any one
# service's UI. It is a static library of its own rather than part of NdmspcCore:
# FTXUI has no business inside the ROOT-dictionary'd core library, nor in the link
# line of that library's other consumers.
#
# Threads is already resolved globally (cmake/base.cmake sets
# THREADS_PREFER_PTHREAD_FLAG and calls find_package(Threads REQUIRED)), so
# Threads::Threads is available here without a second find_package.

add_library(NdmspcTuiFtxui STATIC ftxui_impl.cxx)

# FTXUI's own headers must not add warnings to our builds, even with
# ENABLE_STRICT_WARNINGS=ON. SYSTEM makes them -isystem for this target, and the
# interface property below does the same for every consumer: it is set explicitly
# because target_include_directories only propagates SYSTEM to consumers from CMake
# 3.25, while this project's declared floor is 3.24. PUBLIC so a TUI that links this
# target gets the headers with no include_directories of its own.
target_include_directories(NdmspcTuiFtxui SYSTEM PUBLIC ${FTXUI_INCLUDE_DIRS})
set_target_properties(NdmspcTuiFtxui PROPERTIES
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${FTXUI_INCLUDE_DIRS}")

target_link_libraries(NdmspcTuiFtxui PUBLIC Threads::Threads)
