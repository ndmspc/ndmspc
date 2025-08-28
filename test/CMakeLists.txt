

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
# include_directories(${MY_INCLUDE_DIRS})
#
#
file(COPY ${CMAKE_SOURCE_DIR}/macros/test/cernstaff.root
DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)
add_executable(test_NHnSparseBase test_NHnSparseBase.cxx)
target_link_libraries(test_NHnSparseBase gtest_main ${MY_EXTERNAL_LIBS})
target_include_directories(test_NHnSparseBase PRIVATE ${MY_INCLUDE_DIRS})
add_test(NAME NHnSparseBaseTest COMMAND test_NHnSparseBase)
