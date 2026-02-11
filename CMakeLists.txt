cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}"
      CACHE PATH "Default installation" FORCE)
endif()

project(ndmspc VERSION 1.1.0 DESCRIPTION "NDMSPC")
set(PROJECT_VERSION_RELEASE rc1)

option(USE_LEGACY "Compile with legacy code" OFF)
option(WITH_TEST "Compile with tests" OFF)
option(WITH_SERVER "Compile with server part" ON)
option(WITH_PARQUET "Compile with Parquet support" OFF)

option(WITH_NUMCAL "Compile with numcal support" OFF)

# option to use strict warning flags
option(ENABLE_STRICT_WARNINGS "Enable strict warning flags" OFF)

include(cmake/base.cmake)
include(cmake/deps.cmake)

add_subdirectory(base)
add_subdirectory(core)
if(WITH_NUMCAL)
  add_subdirectory(numcal)
endif()

if(WITH_SERVER)
  add_subdirectory(http)
endif()
add_subdirectory(hep)
if(WITH_TEST)
  find_package(GTest)
  if(GTest_FOUND)
    enable_testing()
    add_subdirectory(test)
  endif()
endif()
if(USE_LEGACY)
  message(STATUS "Compiling with legacy code")
  add_subdirectory(legacy/core)
endif()
add_subdirectory(doc)

# profile.d
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.sh.profile.d.in"
  "${PROJECT_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh"
)


if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS)
  INSTALL(DIRECTORY tutorial DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS)
  INSTALL(FILES ${PROJECT_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh DESTINATION /etc/profile.d/)
  INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
  INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION /etc/systemd/user/)
  INSTALL(FILES etc/systemd/http DESTINATION /etc/${PROJECT_NAME}/)
  INSTALL(FILES etc/systemd/http.default DESTINATION /etc/${PROJECT_NAME}/)
  INSTALL(FILES etc/systemd/http.stress DESTINATION /etc/${PROJECT_NAME}/)
  INSTALL(FILES etc/systemd/ndmspc-notebook@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
  INSTALL(FILES etc/systemd/ndmspc-notebook@.service DESTINATION /etc/systemd/user/)
  INSTALL(FILES etc/systemd/notebook DESTINATION /etc/${PROJECT_NAME}/)
  INSTALL(FILES etc/systemd/notebook.default DESTINATION /etc/${PROJECT_NAME}/)
else()
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX} USE_SOURCE_PERMISSIONS)
endif()
INSTALL(FILES scripts/ndmspc-run PERMISSIONS
OWNER_EXECUTE OWNER_WRITE OWNER_READ
GROUP_EXECUTE GROUP_READ
WORLD_EXECUTE WORLD_READ
DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
if(USE_LEGACY)
  INSTALL(FILES scripts/ndmspc PERMISSIONS
OWNER_EXECUTE OWNER_WRITE OWNER_READ
GROUP_EXECUTE GROUP_READ
WORLD_EXECUTE WORLD_READ
DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
  INSTALL(FILES scripts/ndmspc-run.sh PERMISSIONS
OWNER_EXECUTE OWNER_WRITE OWNER_READ
GROUP_EXECUTE GROUP_READ
WORLD_EXECUTE WORLD_READ
DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
  INSTALL(FILES scripts/ndmspc-slurm-run-jobs PERMISSIONS
OWNER_EXECUTE OWNER_WRITE OWNER_READ
GROUP_EXECUTE GROUP_READ
WORLD_EXECUTE WORLD_READ
DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
  INSTALL(FILES scripts/ndmspc-slurm-run-single PERMISSIONS
OWNER_EXECUTE OWNER_WRITE OWNER_READ
GROUP_EXECUTE GROUP_READ
WORLD_EXECUTE WORLD_READ
DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
endif()
