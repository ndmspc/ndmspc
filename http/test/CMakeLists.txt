file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

# Data the tests open relative to the build dir.
file(COPY ${CMAKE_SOURCE_DIR}/macros/examples/cernstaff/cernstaff.root
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

set(NDMSPC_TEST_EXTRA_INCLUDES ${CMAKE_SOURCE_DIR}/core ${CMAKE_SOURCE_DIR}/http)
set(NDMSPC_TEST_EXTRA_LIBS NdmspcHttp)
set(NDMSPC_TEST_IS_HTTP TRUE)
ndmspc_add_tests(${SRCS})

# A tool test loads its macro from the source tree (see NToolHarness).
target_compile_definitions("test_toolNgnt.cxx" PRIVATE
  NDMSPC_TOOL_MACRO="${CMAKE_SOURCE_DIR}/tools/toolNgnt.C")
target_compile_definitions("test_toolBrowser.cxx" PRIVATE
  NDMSPC_TOOL_BROWSER_MACRO="${CMAKE_SOURCE_DIR}/tools/toolBrowser.C")
