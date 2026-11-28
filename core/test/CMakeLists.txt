file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

# Data/macros the tests open relative to the build dir.
file(COPY ${CMAKE_SOURCE_DIR}/macros/examples/cernstaff/cernstaff.root
     ${CMAKE_CURRENT_SOURCE_DIR}/NTcpTest.C
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

set(NDMSPC_TEST_EXTRA_INCLUDES ${CMAKE_SOURCE_DIR}/core)
set(NDMSPC_TEST_EXTRA_LIBS "")
set(NDMSPC_TEST_IS_HTTP FALSE)
ndmspc_add_tests(${SRCS})
