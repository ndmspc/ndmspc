set(MODULE Hep)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")
set(MY_INCLUDE_DIRS
  ${NLOHMANN_JSON_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/base
  ${CMAKE_SOURCE_DIR}/core
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  NdmspcBase
  NdmspcCore
)
RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

