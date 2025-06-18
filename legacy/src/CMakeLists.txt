set (MY_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}/legacy/base
  ${CMAKE_SOURCE_DIR}/legacy/core
  ${CMAKE_SOURCE_DIR}/legacy/http
  ${CMAKE_SOURCE_DIR}/legacy/ndh
  ${NLOHMANN_JSON_INCLUDE_DIRS}
)
include_directories(${MY_INCLUDE_DIRS}
${OPENTELEMETRY_CPP_INCLUDE_DIRS}
)

set(MY_EXTERNAL_LIBS
  ${OPENTELEMETRY_CPP_LIBRARIES}
  ${CURL_LIBRARIES}
  NdmspcBase_legacy
  NdmspcCore_legacy
  NdmspcHttp_legacy
  NdmspcNdh_legacy
)

file(GLOB EXEC_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cxx")
string(REPLACE ".cxx" "" EXECS "${EXEC_SRCS}")
foreach(t ${EXECS})
  RootBin(${t}-old "${t}.cxx" "${MY_EXTERNAL_LIBS}")
endforeach(t)

