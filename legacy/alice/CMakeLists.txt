set(MODULE ALICE_legacy)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")
set(MY_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/legacy/base
  ${CMAKE_SOURCE_DIR}/legacy/http
  ${CMAKE_SOURCE_DIR}/legacy/core
  ${OPENTELEMETRY_CPP_INCLUDE_DIRS}
  ${OpenMP_INCLUDE_DIRS}
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  NdmspcBase_legacy
  NdmspcHttp_legacy
  NdmspcCore_legacy
  ${OPENTELEMETRY_CPP_LIBRARIES}
  OpenMP::OpenMP_CXX
)
RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

