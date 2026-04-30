# FindZEROMQ.cmake
#
# This module defines:
#   ZEROMQ_FOUND          - Whether ZeroMQ was found
#   ZEROMQ_INCLUDE_DIRS   - ZeroMQ include directories
#   ZEROMQ_LIBRARIES      - ZeroMQ libraries

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_ZEROMQ QUIET libzmq)
endif()

set(_ZEROMQ_ROOT_HINTS)
if(ZEROMQ_ROOT)
  list(APPEND _ZEROMQ_ROOT_HINTS "${ZEROMQ_ROOT}")
endif()
if(DEFINED ENV{ZEROMQ_ROOT})
  list(APPEND _ZEROMQ_ROOT_HINTS "$ENV{ZEROMQ_ROOT}")
endif()

find_path(ZEROMQ_INCLUDE_DIRS
  NAMES zmq.h
  HINTS
    ${_ZEROMQ_ROOT_HINTS}
    ${PC_ZEROMQ_INCLUDEDIR}
    ${PC_ZEROMQ_INCLUDE_DIRS}
  PATH_SUFFIXES include
)

find_library(ZEROMQ_LIBRARIES
  NAMES zmq libzmq
  HINTS
    ${_ZEROMQ_ROOT_HINTS}
    ${PC_ZEROMQ_LIBDIR}
    ${PC_ZEROMQ_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  ZEROMQ
  REQUIRED_VARS ZEROMQ_INCLUDE_DIRS ZEROMQ_LIBRARIES
  FAIL_MESSAGE "ZeroMQ development files are required (zmq.h and libzmq). Set ZEROMQ_ROOT to the ZeroMQ installation prefix if needed."
)

mark_as_advanced(ZEROMQ_INCLUDE_DIRS ZEROMQ_LIBRARIES)
