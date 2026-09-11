set(HTTPLIB_VERSION "v0.54.1")
set(JWT_CPP_VERSION "v0.7.2")
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

if (WITH_HTTP OR WITH_AI)
  find_package(OpenSSL REQUIRED)

  # httplib >= 0.54 is required for the WebSocket client's client-certificate
  # (mTLS) support, which older system packages (e.g. 0.48) do not provide.
  # Default to the pinned upstream release; FindHTTPLIB.cmake additionally rejects
  # system headers older than the required version.
  option(USE_SYSTEM_HTTPLIB "Use system-installed HTTPLIB" OFF)

  # Fetched without its CMake subproject: httplib is header-only, so its install()
  # rules must never register (NDMSPC's install prefix is the source root, and they
  # would otherwise copy httplib.h, cmake config and doc/license files into it).
  ndmspc_find_or_fetch(
    NAME HTTPLIB
    VERSION ${HTTPLIB_VERSION}
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/${HTTPLIB_VERSION}.tar.gz
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    INCLUDE_SUBDIR .
    REQUIRED
    NO_ADD_SUBDIRECTORY
  )
endif()

if (WITH_HTTP)
  # jwt-cpp is a header-only dependency, used via JWT_CPP_INCLUDE_DIRS only.
  # Fetch it without adding its CMake subproject so its own install() rules are
  # never registered: NDMSPC's CMAKE_INSTALL_PREFIX defaults to the source root,
  # and jwt-cpp's install would otherwise copy files into cmake/ and include/.
  ndmspc_find_or_fetch(
    NAME JWT_CPP
    VERSION ${JWT_CPP_VERSION}
    URL https://github.com/Thalhammer/jwt-cpp/archive/refs/tags/${JWT_CPP_VERSION}.tar.gz
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    INCLUDE_SUBDIR include
    REQUIRED
    NO_ADD_SUBDIRECTORY
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
