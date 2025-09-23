# FindCURL.cmake
#
# This module defines
# CURL_FOUND, whether CURL was found
# CURL_INCLUDE_DIRS, include directories
# CURL_LIBRARIES, link libraries

find_path(CURL_INCLUDE_DIRS
  NAMES curl/curl.h
  PATHS $ENV{CURL_ROOT}/include /usr/local/include /usr/include
)

find_library(CURL_LIBRARIES
  NAMES curl
  PATHS $ENV{CURL_ROOT}/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
CURL DEFAULT_MSG
CURL_LIBRARIES
CURL_INCLUDE_DIRS
)

mark_as_advanced(CURL_INCLUDE_DIRS CURL_LIBRARIES)
