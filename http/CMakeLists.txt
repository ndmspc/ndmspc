set(MODULE Http)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

set(MY_INCLUDE_DIRS
  ${NLOHMANN_JSON_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/..
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  ${NLOHMANN_JSON_LIBRARIES}
  RHTTP
  NdmspcCore
)

RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

add_subdirectory(cli)
