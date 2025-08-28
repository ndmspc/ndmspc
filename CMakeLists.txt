cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}"
      CACHE PATH "Default installation" FORCE)
endif()

project(ndmspc VERSION 1.0.0 DESCRIPTION "NDMSPC")
set(PROJECT_VERSION_RELEASE rc1)

option(USE_LATEST "Compile with latest code" ON)
option(USE_LEGACY "Compile with legacy code" OFF)

include(cmake/cmake_base.cmake)

# TODO: Remove it: Temporary fix for opentelemetry-cpp
add_definitions(-Wno-cpp)

if(PROTOBUF_ROOT)
  find_package(Protobuf CONFIG REQUIRED)
  set(Protobuf_LIBRARIES "${PROTOBUF_ROOT}/lib")
  set(Protobuf_INCLUDE_DIR "${PROTOBUF_ROOT}/include")
endif()
find_package(OpenSSL REQUIRED)
find_package(LIBWEBSOCKETS REQUIRED)
find_package(opentelemetry-cpp CONFIG REQUIRED)
# if(opentelemetry-cpp_FOUND)
message(STATUS "Found opentelemetry-cpp: ${opentelemetry-cpp_VERSION}")
find_package(CURL REQUIRED)
find_package(NLOHMANN_JSON REQUIRED)
# endif()
#find_package(OpenMP REQUIRED)

find_package(Root REQUIRED)
include(ROOTMacros)

#include_directories(${PROJECT_SOURCE_DIR}/Utils)

if(USE_LATEST)
  add_subdirectory(base)
  add_subdirectory(http)
  add_subdirectory(core)
  add_subdirectory(hep)
  add_subdirectory(cli)
  find_package(GTest)
  if(GTest_FOUND)
    enable_testing()
    add_subdirectory(test)
  endif()
endif()
if(USE_LEGACY)
  message(STATUS "Compiling with legacy code")
  add_subdirectory(legacy/base)
  add_subdirectory(legacy/core)
  #add_subdirectory(legacy/analysis)
  add_subdirectory(legacy/http)
  # add_subdirectory(legacy/event)
  add_subdirectory(legacy/ndh)
  add_subdirectory(legacy/alice)
  add_subdirectory(legacy/src)
  add_subdirectory(doc)
endif()


# profile.d
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.sh.profile.d.in"
  "${PROJECT_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh"
)


if(CMAKE_INSTALL_PREFIX STREQUAL "/usr")
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME} USE_SOURCE_PERMISSIONS)
  INSTALL(FILES ${PROJECT_BINARY_DIR}/etc/profile.d/${CMAKE_PROJECT_NAME}.sh DESTINATION /etc/profile.d/)
  INSTALL(FILES etc/systemd/ndmspc-http@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/)
  INSTALL(FILES etc/systemd/ndmspc-http DESTINATION /etc/${PROJECT_NAME}/)
  INSTALL(FILES etc/systemd/ndmspc-http.default DESTINATION /etc/${PROJECT_NAME}/)
  INSTALL(FILES etc/systemd/ndmspc-http.stress DESTINATION /etc/${PROJECT_NAME}/)
else()
  INSTALL(DIRECTORY macros DESTINATION ${CMAKE_INSTALL_PREFIX} USE_SOURCE_PERMISSIONS)
endif()
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
