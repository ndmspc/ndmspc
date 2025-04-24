# FindOpenSSL.cmake
#
# This module defines
# OpenSSL_FOUND, whether OpenSSL was found
# OpenSSL_INCLUDE_DIRS, include directories
# OpenSSL_LIBRARIES, link libraries

find_path(OpenSSL_INCLUDE_DIRS
  NAMES openssl/ssl.h
  PATHS $ENV{OPENSSL_ROOT}/include /usr/local/include /usr/include
)

find_library(OpenSSL_LIBRARIES
  NAMES ssl
  PATHS $ENV{OPENSSL_ROOT}/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OpenSSL DEFAULT_MSG OpenSSL_LIBRARIES OpenSSL_INCLUDE_DIRS
)

mark_as_advanced(OpenSSL_INCLUDE_DIRS OpenSSL_LIBRARIES)
