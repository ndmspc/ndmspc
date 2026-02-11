# FindGINAC.cmake
#
# This module defines
# GINAC_FOUND, whether GINAC was found
# GINAC_INCLUDE_DIRS, include directories
# GINAC_LIBRARIES, link libraries

find_path(GINAC_INCLUDE_DIRS
  NAMES ginac.h
  PATHS $ENV{GINAC_ROOT}/include /usr/local/include /usr/include /usr/include/ginac
)

find_library(GINAC_LIBRARIES
  NAMES ginac
  PATHS $ENV{GINAC_ROOT}/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
GINAC DEFAULT_MSG
GINAC_LIBRARIES
GINAC_INCLUDE_DIRS
)

mark_as_advanced(GINAC_INCLUDE_DIRS GINAC_LIBRARIES)
