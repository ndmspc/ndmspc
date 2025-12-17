

set(MY_INCLUDE_DIRS
  ${THREADS_INCLUDE_DIRS}
  ${GTEST_INCLUDE_DIRS}
  ${ROOT_INCLUDE_DIRS}
  ${CURL_INCLUDE_DIRS}
  ${OpenSSL_INCLUDE_DIRS}
  ${LIBWEBSOCKETS_INCLUDE_DIRS}
  ${OPENTELEMETRY_CPP_INCLUDE_DIRS}
  ${NLOHMANN_JSON_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/base
  ${CMAKE_SOURCE_DIR}/core
  ${CMAKE_SOURCE_DIR}/tutorial
)
set(MY_EXTERNAL_LIBS
  ${THREADS_LIBRARIES}
  ${GTEST_LIBRARIES}
  ${ROOT_LIBRARIES}
  ${OPENTELEMETRY_CPP_LIBRARIES}
  ${CURL_LIBRARIES}
  ${OpenSSL_LIBRARIES}
  ${LIBWEBSOCKETS_LIBRARIES}
  # nlohmann_json::nlohmann_json
  NdmspcBase
  NdmspcCore
)
file(COPY ${CMAKE_SOURCE_DIR}/macros/root/cernstaff/cernstaff.root
DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

foreach(src ${SRCS})
  string(REPLACE ".C" "" testname ${src})
  add_executable(${testname} ${src})
  target_include_directories(${testname} PRIVATE ${MY_INCLUDE_DIRS})
  target_link_directories(${testname} PRIVATE ${ROOT_LIBRARY_DIR})
  target_link_libraries(${testname} gtest_main ${MY_EXTERNAL_LIBS})
  add_test(NAME ${testname} COMMAND ${testname})
endforeach()

