cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}"
      CACHE PATH "Default installation" FORCE)
endif()

project(ndmspc VERSION 1.3.0 DESCRIPTION "NDMSPC")
set(PROJECT_VERSION_RELEASE 0)

option(WITH_HTTP "Compile with HTTP part" ON)
option(WITH_UI "Compile with UI part" OFF)
option(WITH_HEP "Compile with HEP part" ON)
option(WITH_UTILS "Compile with utils part" OFF)
option(WITH_MON "Compile with MON part" OFF)
# option(WITH_PARQUET "Compile with Parquet support" OFF)
option(WITH_TAXI "Compile with Taxi support" OFF)
option(WITH_NUMCAL "Compile with numcal support" OFF)
option(WITH_TEST "Compile with tests" OFF)

option(WITH_JUPYROOT "Compile with JupyROOT support" OFF)
option(BUILD_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" OFF)

# option to use strict warning flags
option(ENABLE_STRICT_WARNINGS "Enable strict warning flags" OFF)

include(cmake/base.cmake)
include(cmake/deps.cmake)

add_subdirectory(core)

if(WITH_HTTP)
  add_subdirectory(http)
endif()

if(WITH_MON)
  add_subdirectory(mon)
endif()

if(WITH_HEP)
  add_subdirectory(hep)
endif()

if(WITH_UTILS)
  add_subdirectory(utils)
endif()

if(WITH_NUMCAL)
  add_subdirectory(numcal)
endif()

if(WITH_TAXI)
  add_subdirectory(taxi)
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

if(WITH_UI)
  add_subdirectory(ui)
endif()

# profile.d
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.sh.profile.d.in"
  "${CMAKE_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh"
)

if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
  INSTALL(FILES ${CMAKE_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh DESTINATION /etc/profile.d/)
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS)
  INSTALL(DIRECTORY tutorial DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS)
  if(WITH_HTTP)
    INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
    INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION /etc/systemd/user/)
    INSTALL(FILES etc/systemd/http DESTINATION /etc/${PROJECT_NAME}/)
    INSTALL(FILES etc/systemd/http.default DESTINATION /etc/${PROJECT_NAME}/)
    INSTALL(FILES etc/systemd/http.ngnt DESTINATION /etc/${PROJECT_NAME}/)
    INSTALL(FILES etc/systemd/http.stress DESTINATION /etc/${PROJECT_NAME}/)
  endif(WITH_HTTP)
  if (WITH_JUPYROOT)
    INSTALL(FILES etc/systemd/ndmspc-notebook@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
    INSTALL(FILES etc/systemd/ndmspc-notebook@.service DESTINATION /etc/systemd/user/)
    INSTALL(FILES etc/systemd/notebook DESTINATION /etc/${PROJECT_NAME}/)
    INSTALL(FILES etc/systemd/notebook.default DESTINATION /etc/${PROJECT_NAME}/)
  endif(WITH_JUPYROOT)
else()
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX} USE_SOURCE_PERMISSIONS)
  INSTALL(DIRECTORY tutorial DESTINATION ${CMAKE_INSTALL_PREFIX} USE_SOURCE_PERMISSIONS)
endif()
