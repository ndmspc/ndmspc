# FindCUBA.cmake
#
# This module defines
# CUBA_FOUND, whether CUBA was found
# CUBA_INCLUDE_DIRS, include directories
# CUBA_LIBRARIES, link libraries

find_path(CUBA_INCLUDE_DIRS
  NAMES cuba.h
  PATHS $ENV{CUBA_ROOT}/include /usr/local/include /usr/include
)

find_library(CUBA_LIBRARIES
  NAMES cuba
  PATHS $ENV{CUBA_ROOT}/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
CUBA DEFAULT_MSG
CUBA_LIBRARIES
CUBA_INCLUDE_DIRS
)

mark_as_advanced(CUBA_INCLUDE_DIRS CUBA_LIBRARIES)
