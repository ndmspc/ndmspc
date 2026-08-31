# FindHTTPLIB.cmake
#
# This module defines
# HTTPLIB_FOUND, whether HTTPLIB was found
# HTTPLIB_INCLUDE_DIRS, include directories

find_path(HTTPLIB_INCLUDE_DIRS
  NAMES httplib.h
  PATHS ${HTTPLIB_ROOT}/include /usr/local/include /usr/include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HTTPLIB DEFAULT_MSG
                                  HTTPLIB_INCLUDE_DIRS)

mark_as_advanced(HTTPLIB_INCLUDE_DIRS)
