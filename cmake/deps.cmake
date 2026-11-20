# Pinned third-party releases. Each URL is verified with the SHA256 of the published archive
# (URL_HASH below): bumping a *_VERSION means recomputing that hash in the same change, e.g.
#   curl -fsSL -o x <url> && sha256sum x
# The git-tag fallback (USE_FAST_FETCH=OFF) cannot be hash-checked and stays tag-based.
#
# The URL is spelled once here, used both by the ndmspc_find_or_fetch() calls below and by
# `make dist`, which vendors the archive into the source tarball (NDMSPC_DEPS_VENDOR_ARGS).
set(HTTPLIB_VERSION "v0.54.1")
set(HTTPLIB_URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/${HTTPLIB_VERSION}.tar.gz)
set(HTTPLIB_URL_HASH "SHA256=7310f5312e1423830d649b38ed028e9db86303a979ccbfdbd1c4b1574f422dfb")
set(JWT_CPP_VERSION "v0.7.2")
set(JWT_CPP_URL https://github.com/Thalhammer/jwt-cpp/archive/refs/tags/${JWT_CPP_VERSION}.tar.gz)
set(JWT_CPP_URL_HASH "SHA256=6e815d86c168eb521a27937d603747dec0ca3c39ffc12d6fa72e2cf78a5b02d2")
set(NLOHMANN_JSON_VERSION "v3.11.3")
set(NLOHMANN_JSON_URL https://github.com/nlohmann/json/releases/download/${NLOHMANN_JSON_VERSION}/include.zip)
set(NLOHMANN_JSON_URL_HASH "SHA256=a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d")
set(CLI11_VERSION "v2.5.0")
set(CLI11_URL https://github.com/CLIUtils/CLI11/archive/refs/tags/${CLI11_VERSION}.tar.gz)
set(CLI11_URL_HASH "SHA256=17e02b4cddc2fa348e5dbdbb582c59a3486fa2b2433e70a0c3bacb871334fd55")
set(FTXUI_VERSION "v7.0.3")
set(FTXUI_URL https://github.com/ArthurSonzogni/FTXUI/releases/download/${FTXUI_VERSION}/ftxui-amalgamated.zip)
set(FTXUI_URL_HASH "SHA256=63d13bb65747bdbf0edf04d83ce490708022371cf378f15fad8489249ea4fd9a")

# Archives `make dist` vendors into the source tarball, as <name> <url> <hash> triples, so a
# build from the tarball configures without network access. All pinned deps are listed, not
# only the enabled ones: the tarball is built with one option set and may be configured with
# another (e.g. WITH_TUI), and every archive here is already pinned and verified.
set(NDMSPC_DEPS_VENDOR_ARGS
  NLOHMANN_JSON "${NLOHMANN_JSON_URL}" "${NLOHMANN_JSON_URL_HASH}"
  CLI11          "${CLI11_URL}"          "${CLI11_URL_HASH}"
  FTXUI          "${FTXUI_URL}"          "${FTXUI_URL_HASH}"
  HTTPLIB        "${HTTPLIB_URL}"        "${HTTPLIB_URL_HASH}"
  JWT_CPP        "${JWT_CPP_URL}"        "${JWT_CPP_URL_HASH}"
)

option(USE_FAST_FETCH "Fast fetch mode" ON)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Disable update step")
include(NdmspcFindOrFetch)

# Where the pinned archives are read from: a source tarball from `make dist` carries them in
# third-party/ (see scripts/fetch-deps.sh), and a checkout that has run `make dist` keeps that
# directory as a cache, so nothing is downloaded twice. Set -DNDMSPC_DEPS_DIR=<dir> to point a
# build at archives that are already on disk. Unset: download them.
if(NOT DEFINED NDMSPC_DEPS_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/third-party")
  set(NDMSPC_DEPS_DIR "${CMAKE_SOURCE_DIR}/third-party")
endif()

# ------------------ Dependencies via find_package or FetchContent ------------------
ndmspc_find_or_fetch(
  NAME NLOHMANN_JSON
  VERSION ${NLOHMANN_JSON_VERSION}
  URL ${NLOHMANN_JSON_URL}
  URL_HASH ${NLOHMANN_JSON_URL_HASH}
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  INCLUDE_SUBDIR include
  REQUIRED
)

ndmspc_find_or_fetch(
  NAME CLI11
  VERSION ${CLI11_VERSION}
  URL ${CLI11_URL}
  URL_HASH ${CLI11_URL_HASH}
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  INCLUDE_SUBDIR include
  REQUIRED
)

# FTXUI backs NDMSPC's terminal user interfaces. Only its implementation lives here:
# core/tui compiles the amalgamated ftxui_all.hpp once (ftxui_impl.cxx) and hands the
# result on, so a TUI needs nothing but a link to NdmspcTuiFtxui.
#
# Only needed when the terminal UIs are enabled. It is fetched in amalgamated
# header-only form: there is nothing to build and nothing to install, so it is
# fetched without adding a CMake subproject - the same rule as httplib/jwt-cpp below
# (NDMSPC's install prefix is the source root, and FTXUI's install() rules would
# otherwise copy headers, CMake config and licence files into it).
#
# USE_SYSTEM_FTXUI is forced OFF: a distribution package installs a *compiled* library
# exposing ftxui/component/*.hpp, not the amalgamated ftxui_all.hpp this build expects,
# so probing for it could only ever pick a header set we cannot compile against.
# Declaring the option here also stops ndmspc_find_or_fetch from re-enabling it with
# its own "ON" default.
if(WITH_TUI)
  option(USE_SYSTEM_FTXUI "Use system-installed FTXUI" OFF)
  if(NOT USE_FAST_FETCH)
    message(FATAL_ERROR
      "FTXUI is only published as an amalgamated release archive "
      "(ftxui-amalgamated.zip), which the git-tag fetch mode cannot produce. "
      "Re-configure with USE_FAST_FETCH=ON (the default).")
  endif()
  ndmspc_find_or_fetch(
    NAME FTXUI
    VERSION ${FTXUI_VERSION}
    URL ${FTXUI_URL}
    URL_HASH ${FTXUI_URL_HASH}
    INCLUDE_SUBDIR .
    REQUIRED
    NO_ADD_SUBDIRECTORY
  )
endif()

if (WITH_HTTP OR WITH_AI)
  # httplib requires OpenSSL >= 3; NDMSPC_OPENSSL=1.1 keeps a gated compatibility build.
  set(NDMSPC_OPENSSL "auto" CACHE STRING "OpenSSL selection: auto, 3 or 1.1")
  set(NDMSPC_OPENSSL_ALLOW_1_1 ON CACHE BOOL "Allow the auto mode to fall back to OpenSSL 1.1")
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
    URL ${HTTPLIB_URL}
    URL_HASH ${HTTPLIB_URL_HASH}
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    INCLUDE_SUBDIR .
    REQUIRED
    NO_ADD_SUBDIRECTORY
  )

  # httplib 0.54 requires OpenSSL >= 3. In the 1.1 compatibility build the only post-1.1
  # symbol it uses is SSL_get1_peer_certificate, which is the same call as
  # SSL_get_peer_certificate in 1.1 (both return an incremented reference).
  # Remove this block together with the OpenSSL 1.1 support.
  if(NDMSPC_OPENSSL_MAJOR VERSION_LESS 3)
    if(USE_SYSTEM_HTTPLIB)
      message(FATAL_ERROR
        "OpenSSL 1.1 requires the bundled httplib (the compatibility shim patches httplib.h); "
        "configure with -DUSE_SYSTEM_HTTPLIB=OFF.")
    endif()

    set(_httplib_header "${HTTPLIB_INCLUDE_DIRS}/httplib.h")
    if(NOT EXISTS "${_httplib_header}")
      message(FATAL_ERROR "OpenSSL 1.1: cannot find the fetched httplib header at '${_httplib_header}'")
    endif()

    file(READ "${_httplib_header}" _httplib_text)
    string(REPLACE
      "#error Sorry, OpenSSL versions prior to 3.0.0 are not supported"
      "#define SSL_get1_peer_certificate SSL_get_peer_certificate /* ndmspc: OpenSSL 1.1 compat; remove with 1.1 support */"
      _httplib_text "${_httplib_text}")
    if(_httplib_text MATCHES "OpenSSL versions prior to 3\\.0\\.0")
      message(FATAL_ERROR
        "OpenSSL 1.1: the httplib compatibility shim no longer matches the fetched header; "
        "update the shim in cmake/deps.cmake.")
    endif()
    file(WRITE "${_httplib_header}" "${_httplib_text}")
    message(STATUS "HTTPLIB: applied the OpenSSL 1.1 compatibility shim")
  endif()

  # On macOS httplib enables its keychain trust-store backend as soon as OpenSSL support is
  # compiled in (CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN in httplib.h): its
  # tls::load_system_certs() then calls CoreFoundation and Security. Those frameworks are not
  # linked implicitly, so every target that compiles httplib with OpenSSL must link them or
  # the link fails with undefined _CF*/_Sec* symbols.
  set(HTTPLIB_PLATFORM_LIBS "")
  if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    find_library(SECURITY_LIBRARY Security REQUIRED)
    set(HTTPLIB_PLATFORM_LIBS ${COREFOUNDATION_LIBRARY} ${SECURITY_LIBRARY})
  endif()
endif()

if (WITH_HTTP)
  # jwt-cpp is a header-only dependency, used via JWT_CPP_INCLUDE_DIRS only.
  # Fetch it without adding its CMake subproject so its own install() rules are
  # never registered: NDMSPC's CMAKE_INSTALL_PREFIX defaults to the source root,
  # and jwt-cpp's install would otherwise copy files into cmake/ and include/.
  ndmspc_find_or_fetch(
    NAME JWT_CPP
    VERSION ${JWT_CPP_VERSION}
    URL ${JWT_CPP_URL}
    URL_HASH ${JWT_CPP_URL_HASH}
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

# aliBuild's ZeroMQ is built with an @rpath install name, and it reaches the link line only as
# the absolute library path through NdmspcCore's interface - which, unlike a --link-path
# directory, does not add a runtime search entry. Without this the installed binaries fail with
# "Library not loaded: @rpath/libzmq.5.dylib".
if(APPLE)
  foreach(_zeromq_lib IN LISTS ZEROMQ_LIBRARIES)
    if(IS_ABSOLUTE "${_zeromq_lib}")
      get_filename_component(_zeromq_libdir "${_zeromq_lib}" DIRECTORY)
      list(APPEND CMAKE_INSTALL_RPATH "${_zeromq_libdir}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES CMAKE_INSTALL_RPATH)
endif()

find_package(Root REQUIRED)
include(ROOTMacros)
