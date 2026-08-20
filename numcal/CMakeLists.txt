set(MODULE Numcal)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

message(STATUS "Compiling with Numcal support")
find_package(GINAC REQUIRED)
find_package(CUBA REQUIRED)

set(MY_INCLUDE_DIRS
  ${CUBA_INCLUDE_DIRS}
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  ${CUBA_LIBRARIES}
  # RHTTP
  NdmspcCore
)

RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

add_subdirectory(cli)

