set (MY_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/Core
  ${CMAKE_SOURCE_DIR}/Http
  ${CMAKE_SOURCE_DIR}/Ndh
)
include_directories(${MY_INCLUDE_DIRS})

set(MY_EXTERNAL_LIBS
  NdmspcCore
  NdmspcHttp
  NdmspcNdh
  nlohmann_json::nlohmann_json
)

file(GLOB EXEC_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cxx" )
string(REPLACE ".cxx" "" EXECS "${EXEC_SRCS}")
foreach(t ${EXECS})
  RootBin(${t} "${t}.cxx" "${MY_EXTERNAL_LIBS}")
endforeach(t)

