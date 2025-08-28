set(MY_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/base
  ${CMAKE_SOURCE_DIR}/core
  ${CMAKE_SOURCE_DIR}/http
  ${CMAKE_SOURCE_DIR}/hep
)

set(MY_EXTERNAL_LIBS
  NdmspcBase
  NdmspcCore
  NdmspcHttp
  NdmspcHep
)

include_directories(${MY_INCLUDE_DIRS})
file(GLOB EXEC_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cxx")
string(REPLACE ".cxx" "" EXECS "${EXEC_SRCS}")
foreach(t ${EXECS})
  RootBin(${t} "${t}.cxx" "${MY_EXTERNAL_LIBS}")
endforeach(t)

