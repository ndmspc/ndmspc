# FindLIBUV.cmake
#
# This module defines
# LIBUV_FOUND, whether LIBUV was found
# LIBUV_INCLUDE_DIRS, include directories
# LIBUV_LIBRARIES, link libraries

find_path(LIBUV_INCLUDE_DIRS
  NAMES uv.h
  PATHS $ENV{LIBUV_ROOT}/include /usr/local/include /usr/include
)

find_library(LIBUV_LIBRARIES
  NAMES uv
  PATHS $ENV{LIBUV_ROOT}/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
LIBUV DEFAULT_MSG
LIBUV_LIBRARIES
LIBUV_INCLUDE_DIRS
)

mark_as_advanced(LIBUV_INCLUDE_DIRS LIBUV_LIBRARIES)
