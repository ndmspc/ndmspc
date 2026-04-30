

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
if(WITH_NUMCAL)
  list(APPEND MY_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/numcal ${GINAC_INCLUDE_DIRS})
  list(APPEND MY_EXTERNAL_LIBS NdmspcNumcal ${GINAC_LIBRARIES})
endif()
file(COPY ${CMAKE_SOURCE_DIR}/macros/root/cernstaff/cernstaff.root
DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

if(NOT WITH_NUMCAL)
  list(FILTER SRCS EXCLUDE REGEX "test_NNumcal.*\\.cxx$")
endif()

foreach(src ${SRCS})
  string(REPLACE ".C" "" testname ${src})
  add_executable(${testname} ${src})
  target_include_directories(${testname} PRIVATE ${MY_INCLUDE_DIRS})
  target_link_directories(${testname} PRIVATE ${ROOT_LIBRARY_DIR})
  target_link_libraries(${testname} gtest_main ${MY_EXTERNAL_LIBS})
  if(src MATCHES "^test_06_")
    # NBinnings mode matrix: thread1, thread4, ipc4, tcp4 x onlyOdd false/true
    add_test(NAME ${testname}_thread1_all COMMAND ${testname})
    set_tests_properties(${testname}_thread1_all PROPERTIES ENVIRONMENT "NDMSPC_TEST_SCENARIO=thread1;NDMSPC_TEST_ONLY_ODD=0")

    add_test(NAME ${testname}_thread1_odd COMMAND ${testname})
    set_tests_properties(${testname}_thread1_odd PROPERTIES ENVIRONMENT "NDMSPC_TEST_SCENARIO=thread1;NDMSPC_TEST_ONLY_ODD=1")

    add_test(NAME ${testname}_thread4_all COMMAND ${testname})
    set_tests_properties(${testname}_thread4_all PROPERTIES ENVIRONMENT "NDMSPC_TEST_SCENARIO=thread4;NDMSPC_TEST_ONLY_ODD=0")

    add_test(NAME ${testname}_thread4_odd COMMAND ${testname})
    set_tests_properties(${testname}_thread4_odd PROPERTIES ENVIRONMENT "NDMSPC_TEST_SCENARIO=thread4;NDMSPC_TEST_ONLY_ODD=1")

    add_test(NAME ${testname}_ipc4_all COMMAND ${testname})
    set_tests_properties(${testname}_ipc4_all PROPERTIES ENVIRONMENT "NDMSPC_TEST_SCENARIO=ipc4;NDMSPC_TEST_ONLY_ODD=0")

    add_test(NAME ${testname}_ipc4_odd COMMAND ${testname})
    set_tests_properties(${testname}_ipc4_odd PROPERTIES ENVIRONMENT "NDMSPC_TEST_SCENARIO=ipc4;NDMSPC_TEST_ONLY_ODD=1")

    add_test(NAME ${testname}_tcp4_all COMMAND ${testname})
    set_tests_properties(${testname}_tcp4_all PROPERTIES
      ENVIRONMENT "NDMSPC_TEST_SCENARIO=tcp4;NDMSPC_TEST_ONLY_ODD=0"
      TIMEOUT 20)

    add_test(NAME ${testname}_tcp4_odd COMMAND ${testname})
    set_tests_properties(${testname}_tcp4_odd PROPERTIES
      ENVIRONMENT "NDMSPC_TEST_SCENARIO=tcp4;NDMSPC_TEST_ONLY_ODD=1"
      TIMEOUT 20)
  elseif(src MATCHES "^test_0[1-5]_")
    add_test(NAME ${testname}_thread1 COMMAND ${testname})
    set_tests_properties(${testname}_thread1 PROPERTIES ENVIRONMENT "NDMSPC_EXECUTION_MODE=thread;ROOT_MAX_THREADS=1")

    add_test(NAME ${testname}_thread4 COMMAND ${testname})
    set_tests_properties(${testname}_thread4 PROPERTIES ENVIRONMENT "NDMSPC_EXECUTION_MODE=thread;ROOT_MAX_THREADS=4")

    add_test(NAME ${testname}_ipc4 COMMAND ${testname})
    set_tests_properties(${testname}_ipc4 PROPERTIES ENVIRONMENT "NDMSPC_EXECUTION_MODE=ipc;ROOT_MAX_THREADS=4;NDMSPC_MAX_PROCESSES=4")
  else()
    add_test(NAME ${testname} COMMAND ${testname})
  endif()
endforeach()

