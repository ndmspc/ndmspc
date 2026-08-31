set(HTTPLIB_VERSION "v0.18.1")
set(NLOHMANN_JSON_VERSION "v3.11.3")
set(CLI11_VERSION "v2.5.0")

option(USE_FAST_FETCH "Fast fetch mode" ON)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Disable update step")
include(NdmspcFindOrFetch)

# ------------------ Dependencies via find_package or FetchContent ------------------
ndmspc_find_or_fetch(
  NAME NLOHMANN_JSON
  VERSION ${NLOHMANN_JSON_VERSION}
  URL https://github.com/nlohmann/json/releases/download/${NLOHMANN_JSON_VERSION}/include.zip
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  INCLUDE_SUBDIR include
  REQUIRED
)

ndmspc_find_or_fetch(
  NAME CLI11
  VERSION ${CLI11_VERSION}
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/${CLI11_VERSION}.tar.gz
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  INCLUDE_SUBDIR include
  REQUIRED
)

if (WITH_AI)
  find_package(OpenSSL REQUIRED)

  ndmspc_find_or_fetch(
    NAME HTTPLIB
    VERSION ${HTTPLIB_VERSION}
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/${HTTPLIB_VERSION}.tar.gz
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    INCLUDE_SUBDIR .
    REQUIRED
  )
endif()

# if(WITH_OPENTELEMETRY)
#   # TODO: Remove it: Temporary fix for opentelemetry-cpp
#   add_definitions(-Wno-cpp)
#   message(STATUS "Compiling with OpenTelemetry support")
#   find_package(opentelemetry-cpp CONFIG REQUIRED)
#   message(STATUS "Found opentelemetry-cpp: ${opentelemetry-cpp_VERSION}")
# endif()

message(STATUS "Compiling with ZeroMQ IPC support")
find_package(ZEROMQ REQUIRED)

find_package(Root REQUIRED)
include(ROOTMacros)
