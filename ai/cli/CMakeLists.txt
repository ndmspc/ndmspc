set(MY_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/_deps/httplib-src/
)

set(MY_EXTERNAL_LIBS
  NdmspcCore
  NdmspcAiLLM
  NdmspcAiAgents
)

include_directories(${MY_INCLUDE_DIRS})
file(GLOB EXEC_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cxx")
string(REPLACE ".cxx" "" EXECS "${EXEC_SRCS}")
foreach(t ${EXECS})
  RootBin(${t} "${t}.cxx" "${MY_EXTERNAL_LIBS}")
endforeach(t)

