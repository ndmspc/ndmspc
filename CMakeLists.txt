cmake_minimum_required(VERSION 3.24 FATAL_ERROR)


# Resolves the version from git, the VERSION file (tarball/Docker) or -DNDMSPC_VERSION
# before project() needs the numeric part.
include(cmake/version.cmake)
project(ndmspc VERSION ${PROJECT_VERSION} DESCRIPTION "NDMSPC")

message(STATUS "Project version: ${NDMSPC_VERSION}-${NDMSPC_VERSION_RELEASE}")

option(WITH_HTTP "Compile with HTTP part" ON)
option(WITH_UI "Compile with UI part" ON)
option(WITH_TUI "Compile the terminal UIs (http/tui)" OFF)
option(WITH_HEP "Compile with HEP part" ON)
# option(WITH_PARQUET "Compile with Parquet support" OFF)
option(WITH_TAXI "Compile with Taxi support" OFF)
option(WITH_NUMCAL "Compile with numcal support" OFF)
option(WITH_AI "Compile with AI support" ON)
option(WITH_TEST "Compile with tests" OFF)

option(WITH_JUPYROOT "Compile with JupyROOT support" OFF)
option(BUILD_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" OFF)
option(DOCUMENTATION_WARN_AS_ERROR "Fail the documentation build on Doxygen warnings" OFF)

# option to use strict warning flags
option(ENABLE_STRICT_WARNINGS "Enable strict warning flags" OFF)

include(cmake/base.cmake)


add_subdirectory(core)

if(WITH_HTTP)
  add_subdirectory(http)
endif()

if(WITH_HEP)
  add_subdirectory(hep)
endif()

if(WITH_NUMCAL)
  add_subdirectory(numcal)
endif()

if(WITH_TAXI)
  add_subdirectory(taxi)
endif()

if(WITH_AI)
  add_subdirectory(ai)
endif()

if(WITH_UI)
  add_subdirectory(ui)
endif()

if(WITH_TEST)
  find_package(GTest)
  if(GTest_FOUND)
    enable_testing()
    add_subdirectory(test)
  endif()
endif()

if(BUILD_DOCUMENTATION)
  add_subdirectory(doc)
endif()

# profile.d
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.sh.profile.d.in"
  "${CMAKE_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh"
)

# Runs all tutorial macros in the current directory; shipped so a fresh installation
# can reproduce the set of files the tutorials produce (see ci/Dockerfile.base).
INSTALL(PROGRAMS scripts/ndmspc-tutorials.sh DESTINATION bin RENAME ndmspc-tutorials)

# Builds the ACLiC libraries the server loads for the installed tool macros; the container image
# runs it once, and a non-container installation can run it after an upgrade.
INSTALL(PROGRAMS scripts/precompile-macros.sh DESTINATION share/${PROJECT_NAME}/scripts)

if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
  INSTALL(FILES ${CMAKE_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh DESTINATION /etc/profile.d/)
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS
          PATTERN ".precompiled" EXCLUDE)
  INSTALL(DIRECTORY tutorial DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS)
  if(WITH_HTTP)
    INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
    INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION /etc/systemd/user/)
    INSTALL(FILES etc/systemd/http DESTINATION /etc/${PROJECT_NAME}/)
    # Per-instance settings for ndmspc-http@<instance>.service: http.ngnt is the instance
    # enabled by default, http.example is the template for running another server on
    # another port.
    INSTALL(FILES etc/systemd/http.ngnt DESTINATION /etc/${PROJECT_NAME}/)
    INSTALL(FILES etc/systemd/http.example DESTINATION /etc/${PROJECT_NAME}/)
  endif(WITH_HTTP)
  if (WITH_JUPYROOT)
    INSTALL(FILES etc/systemd/ndmspc-notebook@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
    INSTALL(FILES etc/systemd/ndmspc-notebook@.service DESTINATION /etc/systemd/user/)
    INSTALL(FILES etc/systemd/notebook DESTINATION /etc/${PROJECT_NAME}/)
    INSTALL(FILES etc/systemd/notebook.default DESTINATION /etc/${PROJECT_NAME}/)
  endif(WITH_JUPYROOT)
else()
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX} USE_SOURCE_PERMISSIONS
          PATTERN ".precompiled" EXCLUDE)
  INSTALL(DIRECTORY tutorial DESTINATION ${CMAKE_INSTALL_PREFIX} USE_SOURCE_PERMISSIONS)
endif()
