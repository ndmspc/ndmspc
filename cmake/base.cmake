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

# Version reported by the binaries (aliBuild passes a git describe/tag value; a local build
# derives it from git, falling back to the numeric project version).
if(NOT NDMSPC_VERSION)
  execute_process(
    COMMAND git describe --tags --long
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _git_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _git_result)
  if(_git_result EQUAL 0 AND _git_describe MATCHES "^v?(.+)-([0-9]+)-g([0-9a-fA-F]+)$")
    set(NDMSPC_VERSION "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_2 STREQUAL "0")
      set(NDMSPC_VERSION_RELEASE "${CMAKE_MATCH_3}")
    else()
      set(NDMSPC_VERSION_RELEASE "${CMAKE_MATCH_2}-g${CMAKE_MATCH_3}")
    endif()
  endif()

  if(NOT NDMSPC_VERSION)
    execute_process(
      COMMAND git describe --always
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _git_always
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _git_always_result)
    if(_git_always_result EQUAL 0 AND _git_always)
      string(REGEX REPLACE "^v" "" NDMSPC_VERSION "${_git_always}")
    endif()
  endif()
endif()
if(NOT NDMSPC_VERSION)
  set(NDMSPC_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
endif()
if(NOT NDMSPC_VERSION_RELEASE)
  set(NDMSPC_VERSION_RELEASE "${PROJECT_VERSION_RELEASE}")
endif()

# RPM needs a numeric Version (no '-'); a tag-style suffix and the commits/hash go to Release.
# e.g. 1.4.0-rc46 + 24-g6a9867d -> Version 1.4.0, Release 0.rc46.24.g6a9867d.
set(NDMSPC_RPM_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(NDMSPC_RPM_RELEASE "${PROJECT_VERSION_RELEASE}")
if(NDMSPC_VERSION MATCHES "^v?([0-9]+(\\.[0-9]+)*)(-(.+))?$")
  set(NDMSPC_RPM_VERSION "${CMAKE_MATCH_1}")
  set(_rpm_suffix "${CMAKE_MATCH_4}")
  set(_rpm_release "${NDMSPC_VERSION_RELEASE}")
  if(_rpm_suffix)
    set(_rpm_release "0.${_rpm_suffix}")
    if(NDMSPC_VERSION_RELEASE)
      string(APPEND _rpm_release ".${NDMSPC_VERSION_RELEASE}")
    endif()
  endif()
  if(_rpm_release)
    string(REPLACE "-" "." _rpm_release "${_rpm_release}")
    set(NDMSPC_RPM_RELEASE "${_rpm_release}")
  endif()
endif()
string(REPLACE "-" "." NDMSPC_RPM_VERSION "${NDMSPC_RPM_VERSION}")
string(REPLACE "-" "." NDMSPC_RPM_RELEASE "${NDMSPC_RPM_RELEASE}")

# Main header
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.h.in"
  "${CMAKE_BINARY_DIR}/include/${CMAKE_PROJECT_NAME}/${CMAKE_PROJECT_NAME}.h"
)
INSTALL(FILES
  "${CMAKE_BINARY_DIR}/include/${CMAKE_PROJECT_NAME}/${CMAKE_PROJECT_NAME}.h"
  DESTINATION "${CMAKE_INSTALL_HEADER_DIR}"
)

# Spec file
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

add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)
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
