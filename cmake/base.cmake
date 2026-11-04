# CMAKE base

if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}"
      CACHE PATH "Default installation" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
"${CMAKE_SOURCE_DIR}/common/cmake/modules"
CACHE STRING "Modules for CMake" FORCE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
"${CMAKE_SOURCE_DIR}/cmake/modules"
CACHE STRING "Modules for CMake" FORCE)

set(CMAKE_INSTALL_HEADER_DIR ${CMAKE_INSTALL_PREFIX}/include/${CMAKE_PROJECT_NAME})

# SET RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# RPM needs a numeric Version (no '-'); the pre-release suffix and the commits/hash go to
# Release. NDMSPC_VERSION never carries the commits (they are in NDMSPC_VERSION_RELEASE),
# so e.g. 1.4.0-rc46 + 24-g6a9867d -> Version 1.4.0, Release 0.rc46.24.g6a9867d, and a
# stable build after the tag -> Release 24.g6a9867d.
set(NDMSPC_RPM_VERSION "${PROJECT_VERSION}")
set(_rpm_release "")
if(NOT PROJECT_VERSION_RELEASE STREQUAL "1")
  set(_rpm_release "0.${PROJECT_VERSION_RELEASE}")
endif()
string(LENGTH "${NDMSPC_VERSION_RELEASE}" _rpm_release_length)
if(NDMSPC_VERSION_RELEASE MATCHES "^[0-9]+-g[0-9a-fA-F]+$")
  # Commits after the tag order after it, so they need no 0. prefix.
  if(_rpm_release STREQUAL "")
    set(_rpm_release "${NDMSPC_VERSION_RELEASE}")
  else()
    string(APPEND _rpm_release ".${NDMSPC_VERSION_RELEASE}")
  endif()
elseif(_rpm_release_length GREATER_EQUAL 6 AND NDMSPC_VERSION_RELEASE MATCHES "^[0-9a-fA-F]+$")
  # Building exactly at a tag: the binaries report that tag's commit, but an RPM Release
  # must be ordered, so keep the numeric project release: 1.4.2-1, never 1.4.2-ccf8279.
elseif(NOT NDMSPC_VERSION_RELEASE STREQUAL "" AND NOT NDMSPC_VERSION_RELEASE STREQUAL "1"
       AND NOT NDMSPC_VERSION_RELEASE STREQUAL PROJECT_VERSION_RELEASE)
  if(_rpm_release STREQUAL "")
    # A pre-release suffix (rc1, beta2) orders before the final release; CMake's regex has
    # no {n,m}, hence the plain +.
    if(NDMSPC_VERSION_RELEASE MATCHES "^[0-9]+(\\.[0-9]+)*$")
      set(_rpm_release "${NDMSPC_VERSION_RELEASE}")
    else()
      set(_rpm_release "0.${NDMSPC_VERSION_RELEASE}")
    endif()
  else()
    string(APPEND _rpm_release ".${NDMSPC_VERSION_RELEASE}")
  endif()
endif()
if(_rpm_release STREQUAL "")
  set(_rpm_release "1")
endif()
string(REPLACE "-" "." NDMSPC_RPM_VERSION "${NDMSPC_RPM_VERSION}")
string(REPLACE "-" "." NDMSPC_RPM_RELEASE "${_rpm_release}")

# Main header
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.h.in"
  "${CMAKE_BINARY_DIR}/include/${CMAKE_PROJECT_NAME}/${CMAKE_PROJECT_NAME}.h"
)
INSTALL(FILES
  "${CMAKE_BINARY_DIR}/include/${CMAKE_PROJECT_NAME}/${CMAKE_PROJECT_NAME}.h"
  DESTINATION "${CMAKE_INSTALL_HEADER_DIR}"
)

# Spec file. A pinned ndmspc-ui page must survive into rpmbuild, which configures from
# the tarball in a separate step; a default version is left to resolve there (the
# v<major>.<minor>-stable fallback would otherwise be skipped for no reason).
if(NDMSPC_UI_VERSION_PINNED)
  set(NDMSPC_SPEC_UI_VERSION_ARG "-DNDMSPC_UI_VERSION=${NDMSPC_UI_VERSION}")
else()
  set(NDMSPC_SPEC_UI_VERSION_ARG "")
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.spec.in"
  "${CMAKE_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.spec"
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# This is the preferred way
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
message("-- Found C++ standard: ${CMAKE_CXX_STANDARD}")

set(STRICT_WARNING_FLAGS
    -Wall
    -Wextra
    -Wno-cpp
    -Wpedantic
)

if(ENABLE_STRICT_WARNINGS)
  list(APPEND STRICT_WARNING_FLAGS -Wpedantic)

  if(WARNINGS_AS_ERRORS)
    list(APPEND STRICT_WARNING_FLAGS -Werror)
  endif()

endif()

add_compile_options(${STRICT_WARNING_FLAGS})

# add the binary tree to the search path for include files
# include_directories("${CMAKE_BINARY_DIR}")
include_directories("${CMAKE_BINARY_DIR}/include")

# Setting libdir
set(CMAKE_INSTALL_LIBDIR lib)
if(CMAKE_INSTALL_PREFIX STREQUAL "/usr" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(CMAKE_INSTALL_LIBDIR lib64)
endif()

# CMAKE_INSTALL_RPATH_USE_LINK_PATH covers the external dependencies, not NDMSPC's own library
# directory. On macOS the shared libraries are recorded with an @rpath install name, so the
# installed binaries and libraries need an explicit rpath entry for it; without one dyld
# searches only the dependency directories and fails with "Library not loaded:
# @rpath/libNdmspc*.so". @loader_path makes the install tree relocatable and resolves
# correctly from both bin/ and lib/.
if(APPLE)
  set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake")

set(CPACK_SOURCE_GENERATOR "TGZ")
# The source tarball name must match %{name}-%{version} for `%setup` in the RPM spec.
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${NDMSPC_RPM_VERSION}")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${NDMSPC_RPM_VERSION}")

set(CPACK_SOURCE_IGNORE_FILES
  "/build/"
  "/[.]git/"
  "/[.]vscode/"
  "/bin/"
  "/lib/"
  "lib64/"
  "/tmp/"
  "/[.]cache/"
  "/[.]ndmspc/"
  "~$"
  "[.]root$"
  ${CPACK_SOURCE_IGNORE_FILES}
)

include(cmake/deps.cmake)

# The packaged source carries VERSION so a build from the tarball (no .git) reports the
# same version as this tree, and (NDMSPC_UI_EMBED, the default) the ndmspc-ui page, so
# building it needs no network. NDMSPC_UI_CANDIDATES comes from cmake/version.cmake, so
# the page picked for the tarball is the one a build would have downloaded.
if(NDMSPC_UI_EMBED)
  add_custom_target(dist
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/write-version.sh"
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/fetch-ui-page.sh" ${NDMSPC_UI_CANDIDATES}
    COMMAND ${CMAKE_MAKE_PROGRAM} package_source)
else()
  add_custom_target(dist
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/write-version.sh"
    COMMAND ${CMAKE_MAKE_PROGRAM} package_source)
endif()
# A normal (not cache) guard: a cached "included" flag would keep the stale CPack config
# files after a reconfigure (e.g. when the version changes).
unset(CMAKE_CPACK_MODULE_INCLUDED CACHE)
if(NOT NDMSPC_CPACK_INCLUDED)
    set(NDMSPC_CPACK_INCLUDED TRUE)
    # Now include CPack. This will process the CPack.cmake module.
    include(CPack)
    message(STATUS "CPack module included by top-level CMakeLists.txt")
else()
    message(STATUS "CPack module already included by another source.")
endif()

add_custom_target(rpm
COMMAND rpmbuild -ta
"${CMAKE_BINARY_DIR}/${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz" --define "_topdir ${CMAKE_BINARY_DIR}"
DEPENDS dist
)

add_custom_target(srpm
COMMAND rpmbuild -ts "${CMAKE_BINARY_DIR}/${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz" --define "_topdir ${CMAKE_BINARY_DIR}"
DEPENDS dist
)
