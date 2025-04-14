# CMAKE base
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
"${PROJECT_SOURCE_DIR}/common/cmake/modules"
CACHE STRING "Modules for CMake" FORCE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
"${PROJECT_SOURCE_DIR}/cmake/modules"
CACHE STRING "Modules for CMake" FORCE)

set(PROJECT_HEADER_DIR ${CMAKE_INSTALL_PREFIX}/include/${PROJECT_NAME})

# SET RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Main header
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.h.in"
  "${PROJECT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.h"
)

# Spec file
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.spec.in"
  "${PROJECT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.spec"
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# enable testing
# enable_testing()
# find_package(GTest REQUIRED)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

message("-- Found C++ standard: ${CMAKE_CXX_STANDARD_DEFAULT}")
if(
CMAKE_CXX_STANDARD_DEFAULT STREQUAL "14" OR
CMAKE_CXX_STANDARD_DEFAULT STREQUAL "17" OR
CMAKE_CXX_STANDARD_DEFAULT STREQUAL "20" OR
CMAKE_CXX_STANDARD_DEFAULT STREQUAL "23" OR
CMAKE_CXX_STANDARD_DEFAULT STREQUAL "26"
)

  message("-- Setting C++ standard: ${CMAKE_CXX_STANDARD_DEFAULT}")
  set(CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD_DEFAULT})
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  # if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  #   add_compile_options(-Wno-deprecated)
  # elseif(MSVC)
  #   add_compile_options(/wd4996)
  # endif()
else()
  message("-- Setting C++ standard: 11")
  set(CMAKE_CXX_STANDARD 11)
endif()

# add the binary tree to the search path for include files
include_directories("${PROJECT_BINARY_DIR}")

# Setting libdir
set(CMAKE_INSTALL_LIBDIR lib)
if(CMAKE_INSTALL_PREFIX STREQUAL "/usr" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(CMAKE_INSTALL_LIBDIR lib64)
endif()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
"${CMAKE_PROJECT_NAME}-${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)

set(CPACK_SOURCE_IGNORE_FILES
"/build/;/.git/;/.vscode/;/bin/;/lib/;lib64/;/tmp/;~$;${CPACK_SOURCE_IGNORE_FILES}"
)
add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)
include(CPack)


add_custom_target(rpm
COMMAND rpmbuild -ta
"${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.gz" --define "_topdir ${CMAKE_BINARY_DIR}"
DEPENDS dist
)

add_custom_target(srpm
COMMAND rpmbuild -ts "${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.gz" --define "_topdir ${CMAKE_BINARY_DIR}"
DEPENDS dist
)
