# FindOpenSSL.cmake
#
# This module defines
# OpenSSL_FOUND, whether OpenSSL was found
# OpenSSL_INCLUDE_DIRS, include directories
# OpenSSL_LIBRARIES, link libraries (both libssl and libcrypto)
#
# It also defines the imported targets OpenSSL::SSL and OpenSSL::Crypto.

find_path(OpenSSL_INCLUDE_DIRS
  NAMES openssl/ssl.h
  PATHS $ENV{OPENSSL_ROOT}/include /usr/local/include /usr/include
)

find_library(OpenSSL_SSL_LIBRARY
  NAMES ssl
  PATHS $ENV{OPENSSL_ROOT}/lib /usr/local/lib /usr/lib
)

find_library(OpenSSL_CRYPTO_LIBRARY
  NAMES crypto
  PATHS $ENV{OPENSSL_ROOT}/lib /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OpenSSL DEFAULT_MSG
  OpenSSL_SSL_LIBRARY OpenSSL_CRYPTO_LIBRARY OpenSSL_INCLUDE_DIRS
)

if(OpenSSL_FOUND)
  set(OpenSSL_LIBRARIES ${OpenSSL_SSL_LIBRARY} ${OpenSSL_CRYPTO_LIBRARY})

  if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
      IMPORTED_LOCATION "${OpenSSL_CRYPTO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenSSL_INCLUDE_DIRS}"
    )
  endif()

  if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::SSL PROPERTIES
      IMPORTED_LOCATION "${OpenSSL_SSL_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenSSL_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
    )
  endif()
endif()

mark_as_advanced(OpenSSL_INCLUDE_DIRS OpenSSL_SSL_LIBRARY OpenSSL_CRYPTO_LIBRARY)

