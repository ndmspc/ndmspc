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
set(CPACK_SOURCE_PACKAGE_FILE_NAME
"${CMAKE_PROJECT_NAME}-${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

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
if(NOT CMAKE_CPACK_MODULE_INCLUDED)
    set(CMAKE_CPACK_MODULE_INCLUDED TRUE CACHE BOOL "Indicates if CPack module has been included.")
    # Now include CPack. This will process the CPack.cmake module.
    include(CPack)
    message(STATUS "CPack module included by top-level CMakeLists.txt")
else()
    message(STATUS "CPack module already included by another source.")
endif()

add_custom_target(rpm
COMMAND rpmbuild -ta
"${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.gz" --define "_topdir ${CMAKE_BINARY_DIR}"
DEPENDS dist
)

add_custom_target(srpm
COMMAND rpmbuild -ts "${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.gz" --define "_topdir ${CMAKE_BINARY_DIR}"
DEPENDS dist
)
