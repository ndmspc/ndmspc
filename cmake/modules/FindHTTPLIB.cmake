# FindHTTPLIB.cmake
#
# This module defines
# HTTPLIB_FOUND, whether a sufficiently recent HTTPLIB was found
# HTTPLIB_INCLUDE_DIRS, include directories
# HTTPLIB_VERSION, version parsed from httplib.h (when available)
#
# httplib >= 0.54 is required: the WebSocket client's client-certificate (mTLS)
# support only exists from that version. Older system headers are rejected so the
# pinned upstream release is fetched instead.

set(HTTPLIB_MIN_VERSION 0.54)

find_path(HTTPLIB_INCLUDE_DIRS
  NAMES httplib.h
  PATHS ${HTTPLIB_ROOT}/include /usr/local/include /usr/include
)

# Parse the real header rather than inheriting an outer variable: deps.cmake also
# defines HTTPLIB_VERSION, but there it holds the pinned release tag.
unset(HTTPLIB_VERSION)
if(HTTPLIB_INCLUDE_DIRS AND EXISTS "${HTTPLIB_INCLUDE_DIRS}/httplib.h")
  file(READ "${HTTPLIB_INCLUDE_DIRS}/httplib.h" _httplib_header LIMIT 8192)
  if(_httplib_header MATCHES "#define CPPHTTPLIB_VERSION \"([0-9]+\\.[0-9]+(\\.[0-9]+)?)\"")
    set(HTTPLIB_VERSION "${CMAKE_MATCH_1}")
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HTTPLIB DEFAULT_MSG
                                  HTTPLIB_INCLUDE_DIRS)

if(HTTPLIB_FOUND AND HTTPLIB_VERSION AND HTTPLIB_VERSION VERSION_LESS HTTPLIB_MIN_VERSION)
  message(STATUS "HTTPLIB: system httplib ${HTTPLIB_VERSION} is older than the required "
                 "${HTTPLIB_MIN_VERSION} (needs mTLS WebSocket support); fetching the pinned release")
  set(HTTPLIB_FOUND FALSE)
endif()

mark_as_advanced(HTTPLIB_INCLUDE_DIRS)
