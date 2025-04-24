set(MODULE Core)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")
set(MY_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/base
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  # Threads::Threads
  NdmspcBase
)
RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

