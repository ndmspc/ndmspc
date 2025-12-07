# CMAKE base
# cmake_policy(SET CMP0074 OLD)
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
INSTALL(FILES
  "${PROJECT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.h"
  DESTINATION "${PROJECT_HEADER_DIR}"
)

# Spec file
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/${CMAKE_PROJECT_NAME}.spec.in"
  "${PROJECT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.spec"
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")

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

if(ENABLE_STRICT_WARNINGS)
  set(STRICT_WARNING_FLAGS
    -Wall             # Enable all common warnings
    # -Wextra           # Enable extra warnings not covered by -Wall
    # -Wpedantic        # Issue all the warnings demanded by strict ISO C and ISO C++
    # -Wno-unused-parameter # Often suppressed for interface functions
    # -Wshadow          # Warn whenever a local variable shadows another variable
    # -Wformat=2        # More strict format string checks
    # -Wundef           # Warn if an undefined identifier is used in an #if directive
    # -Wconversion      # Warn for implicit conversions that may alter a value
    # -Wsign-conversion # Warn for conversions between signed and unsigned that change value
    # -Wold-style-cast  # Warn if a C-style cast is used in C++ code
    # -Wnull-dereference # Warn for potential null dereferences
    # -Wdouble-promotion # Warn about implicit promotions of float to double
    # -Wmissing-declarations # Warn for global functions/variables without declarations
    # -Wnon-virtual-dtor # Warn when a class with virtual functions has a non-virtual destructor
  )
  if (WARNINGS_AS_ERRORS)
    list(APPEND STRICT_WARNING_FLAGS -Werror)
  endif()

  add_compile_options(${STRICT_WARNING_FLAGS})
endif()

# add the binary tree to the search path for include files
include_directories("${PROJECT_BINARY_DIR}")

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
