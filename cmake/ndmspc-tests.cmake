# Test build/registration shared by the per-module test directories.
#
# Each module keeps its tests in <module>/test/ and calls, after setting the module-specific
# variables below:
#
#   set(NDMSPC_TEST_EXTRA_INCLUDES ...)   # optional extra include dirs (e.g. core/http)
#   set(NDMSPC_TEST_EXTRA_LIBS ...)       # module library/libraries to link (e.g. NdmspcHttp)
#   set(NDMSPC_TEST_IS_HTTP FALSE|TRUE)   # adds CPPHTTPLIB_OPENSSL_SUPPORT, RPATH and LD_LIBRARY_PATH
#   ndmspc_add_tests(${SRCS})
#
# The ctest names, environment, timeouts and gtest filters are the ones the single top-level test/
# directory used, so splitting the directory does not change what runs.
include_guard(GLOBAL)

function(ndmspc_register_test testname)
  if(testname MATCHES "^test_0[1-5]_")
    add_test(NAME ${testname}_thread1 COMMAND ${testname})
    set_tests_properties(${testname}_thread1 PROPERTIES ENVIRONMENT "NDMSPC_EXECUTION_MODE=thread;ROOT_MAX_THREADS=1")

    add_test(NAME ${testname}_thread4 COMMAND ${testname})
    set_tests_properties(${testname}_thread4 PROPERTIES ENVIRONMENT "NDMSPC_EXECUTION_MODE=thread;ROOT_MAX_THREADS=4")

    add_test(NAME ${testname}_ipc4 COMMAND ${testname})
    set_tests_properties(${testname}_ipc4 PROPERTIES ENVIRONMENT "NDMSPC_EXECUTION_MODE=ipc;NDMSPC_MAX_PROCESSES=4")
  elseif(testname MATCHES "^test_06_")
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
      TIMEOUT 30)

    add_test(NAME ${testname}_tcp4_odd COMMAND ${testname})
    set_tests_properties(${testname}_tcp4_odd PROPERTIES
      ENVIRONMENT "NDMSPC_TEST_SCENARIO=tcp4;NDMSPC_TEST_ONLY_ODD=1"
      TIMEOUT 30)
  elseif(testname MATCHES "^test_08_")
    add_test(NAME ${testname}_supervisor_first COMMAND ${testname} --gtest_filter=TcpLazyJoinTest.WorkersJoinAfterSupervisorStart)
    set_tests_properties(${testname}_supervisor_first PROPERTIES TIMEOUT 60 ENVIRONMENT "NDMSPC_LOG_CONSOLE=0;NDMSPC_LOG_RUN=1")

    add_test(NAME ${testname}_workers_first COMMAND ${testname} --gtest_filter=TcpLazyJoinTest.SupervisorStartsAfterWorkers)
    set_tests_properties(${testname}_workers_first PROPERTIES TIMEOUT 60 ENVIRONMENT "NDMSPC_LOG_CONSOLE=0;NDMSPC_LOG_RUN=1")

    add_test(NAME ${testname}_worker_interrupt COMMAND ${testname} --gtest_filter=TcpLazyJoinTest.WorkerInterruptionRedistributesTasks)
    set_tests_properties(${testname}_worker_interrupt PROPERTIES TIMEOUT 60 ENVIRONMENT "NDMSPC_LOG_CONSOLE=0;NDMSPC_LOG_RUN=1")

    add_test(NAME ${testname}_thread1 COMMAND ${testname} --gtest_filter=TcpLazyJoinTest.SingleThreadProcessesMacro)
    set_tests_properties(${testname}_thread1 PROPERTIES TIMEOUT 60 ENVIRONMENT "NDMSPC_LOG_CONSOLE=0;NDMSPC_LOG_RUN=1")

    add_test(NAME ${testname}_ipc4 COMMAND ${testname} --gtest_filter=TcpLazyJoinTest.IpcProcessesMacro)
    set_tests_properties(${testname}_ipc4 PROPERTIES TIMEOUT 60 ENVIRONMENT "NDMSPC_LOG_CONSOLE=0;NDMSPC_LOG_RUN=1")
  else()
    add_test(NAME ${testname} COMMAND ${testname})
    if(NDMSPC_TEST_IS_HTTP)
      if(testname MATCHES "test_NWsClient|test_NOidcToken")
        set_tests_properties(${testname} PROPERTIES
          ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/utils:${CMAKE_BINARY_DIR}/http:${CMAKE_BINARY_DIR}/core:$ENV{LD_LIBRARY_PATH}")
      else()
        set_tests_properties(${testname} PROPERTIES
          ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/http:${CMAKE_BINARY_DIR}/core:$ENV{LD_LIBRARY_PATH}")
      endif()
    endif()
  endif()
endfunction()

function(ndmspc_add_tests)
  # GoogleTest has two ways of being found and they do not set the same variables:
  # FindGTest.cmake (module mode, e.g. a distro package) defines GTEST_LIBRARIES and lets the
  # linker find a bare `gtest_main`; GTestConfig.cmake (config mode, what Homebrew and other
  # modern installs ship) defines no variables at all - only the imported targets
  # GTest::gtest / GTest::gtest_main. Linking the bare name there fails with
  # "ld: library 'gtest_main' not found", so select the target when it exists.
  if(TARGET GTest::gtest_main)
    set(_gtest_main_target GTest::gtest_main)
  else()
    set(_gtest_main_target gtest_main)
  endif()

  set(_common_include_dirs
    ${THREADS_INCLUDE_DIRS}
    ${GTEST_INCLUDE_DIRS}
    ${ROOT_INCLUDE_DIRS}
    ${OpenSSL_INCLUDE_DIRS}
    ${OPENTELEMETRY_CPP_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
    ${HTTPLIB_INCLUDE_DIRS}
    ${JWT_CPP_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/cmake/include
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/tutorials
    ${NDMSPC_TEST_EXTRA_INCLUDES})
  set(_common_libs
    ${THREADS_LIBRARIES}
    ${GTEST_LIBRARIES}
    ${ROOT_LIBRARIES}
    ${OPENTELEMETRY_CPP_LIBRARIES}
    ${OpenSSL_LIBRARIES}
    ${HTTPLIB_PLATFORM_LIBS}
    NdmspcCore
    ${NDMSPC_TEST_EXTRA_LIBS})

  foreach(src ${ARGN})
    # The executable/ctest name keeps the .cxx suffix, matching what the single test/ tree used.
    string(REPLACE ".C" "" testname ${src})
    add_executable(${testname} ${src})
    target_include_directories(${testname} PRIVATE ${_common_include_dirs})
    target_link_directories(${testname} PRIVATE ${ROOT_LIBRARY_DIR})
    target_link_libraries(${testname} ${_gtest_main_target} ${_common_libs})
    if(NDMSPC_TEST_IS_HTTP)
      target_compile_definitions(${testname} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
      if(testname MATCHES "test_NWsClient|test_NOidcToken")
        target_link_libraries(${testname} NdmspcCore)
        set_target_properties(${testname} PROPERTIES
          BUILD_RPATH "$$ORIGIN/../utils;${CMAKE_BINARY_DIR}/http;${CMAKE_BINARY_DIR}/core")
      else()
        set_target_properties(${testname} PROPERTIES
          BUILD_RPATH "${CMAKE_BINARY_DIR}/http;${CMAKE_BINARY_DIR}/core")
      endif()
    endif()
    ndmspc_register_test(${testname})
  endforeach()
endfunction()
