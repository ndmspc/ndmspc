cmake_minimum_required(VERSION 3.11 FATAL_ERROR)

if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}"
      CACHE PATH "Default installation" FORCE)
endif()

project(ndmspc VERSION 0.20250210.0 DESCRIPTION "NDMSPC")
set(PROJECT_VERSION_RELEASE 1)

include(cmake/cmake_base.cmake)

if (PROTOBUF_ROOT)
  find_package(Protobuf CONFIG REQUIRED)
  set(Protobuf_LIBRARIES "${PROTOBUF_ROOT}/lib")
  set(Protobuf_INCLUDE_DIR "${PROTOBUF_ROOT}/include")
endif()

find_package(opentelemetry-cpp CONFIG REQUIRED)
if(opentelemetry-cpp_FOUND)
  message(STATUS "Found opentelemetry-cpp: ${opentelemetry-cpp_VERSION}")
  find_package(CURL REQUIRED)
  find_package(nlohmann_json REQUIRED)
endif()
find_package(OpenMP REQUIRED)

find_package(Root REQUIRED)
include(ROOTMacros)

include_directories(${PROJECT_SOURCE_DIR}/Utils)
add_subdirectory(base)
add_subdirectory(core)
add_subdirectory(http)
add_subdirectory(event)
add_subdirectory(ndh)
add_subdirectory(src)
add_subdirectory(doc)

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
