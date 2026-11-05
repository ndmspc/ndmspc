# FindOpenSSL.cmake
#
# Selects the OpenSSL installation used by ndmspc.
#
# The bundled httplib (v0.54.1) requires OpenSSL >= 3.0, while an aliBuild environment may
# provide OpenSSL 1.1 for ROOT. NDMSPC_OPENSSL controls the choice:
#
#   auto (default)  OpenSSL >= 3 when available; otherwise 1.1 when allowed
#   3               require OpenSSL >= 3 (system first)
#   env             require OpenSSL >= 3 from OPENSSL_ROOT / OPENSSL_ROOT_DIR
#   1.1             require OpenSSL < 3 (compatibility build)
#
# Defines OpenSSL_FOUND, OpenSSL_VERSION_MAJOR, OpenSSL_INCLUDE_DIRS, OpenSSL_LIBRARIES,
# OpenSSL_SSL_LIBRARY, OpenSSL_CRYPTO_LIBRARY and the imported targets OpenSSL::SSL /
# OpenSSL::Crypto. NDMSPC_OPENSSL_MAJOR is the selected major version (3 or 1).

set(NDMSPC_OPENSSL "auto" CACHE STRING "OpenSSL selection: auto, 3, env or 1.1")
set_property(CACHE NDMSPC_OPENSSL PROPERTY STRINGS auto 3 env 1.1)
option(NDMSPC_OPENSSL_ALLOW_1_1 "Allow the auto mode to fall back to OpenSSL 1.1" ON)

# Candidate roots. In the 1.1 compatibility mode the AliBuild prefix comes first; otherwise the
# system installations are preferred so a 1.1 OPENSSL_ROOT in the environment (aliBuild in an
# al9 container) cannot shadow the 3.x one.
set(_openssl_roots "")
if(NDMSPC_OPENSSL STREQUAL "env")
  list(APPEND _openssl_roots "$ENV{OPENSSL_ROOT}" "${OPENSSL_ROOT_DIR}")
elseif(NDMSPC_OPENSSL STREQUAL "1.1")
  list(APPEND _openssl_roots "$ENV{OPENSSL_ROOT}" "${OPENSSL_ROOT_DIR}")
  list(APPEND _openssl_roots /usr/local /usr)
else()
  list(APPEND _openssl_roots /usr /usr/local)
  list(APPEND _openssl_roots "${OPENSSL_ROOT_DIR}" "$ENV{OPENSSL_ROOT}")
endif()
list(REMOVE_DUPLICATES _openssl_roots)
list(FILTER _openssl_roots EXCLUDE REGEX "^$")

set(_os_best_major -1)
set(_os_best_root "")
set(_os_best_inc "")
set(_os_best_ssl "")
set(_os_best_crypto "")
set(_os_best1_major -1)
set(_os_best1_root "")
set(_os_best1_inc "")
set(_os_best1_ssl "")
set(_os_best1_crypto "")

foreach(_root IN LISTS _openssl_roots)
  find_path(_os_inc NAMES openssl/ssl.h PATHS "${_root}/include" NO_DEFAULT_PATH NO_CACHE)
  if(NOT _os_inc)
    continue()
  endif()

  # Debian/Ubuntu multiarch installations put the libraries in lib/<triplet>
  # (e.g. lib/x86_64-linux-gnu), not in lib or lib64.
  set(_os_lib_dirs "${_root}/lib" "${_root}/lib64")
  if(CMAKE_LIBRARY_ARCHITECTURE)
    list(APPEND _os_lib_dirs "${_root}/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
  endif()

  find_library(_os_ssl NAMES ssl PATHS ${_os_lib_dirs} NO_DEFAULT_PATH NO_CACHE)
  find_library(_os_crypto NAMES crypto PATHS ${_os_lib_dirs} NO_DEFAULT_PATH NO_CACHE)
  if(NOT _os_ssl OR NOT _os_crypto)
    continue()
  endif()

  # OPENSSL_VERSION_TEXT is "OpenSSL 3.0.7 ..." / "OpenSSL 1.1.1m ..."; fall back on the first
  # digit of OPENSSL_VERSION_NUMBER (0xMNN00PP0L) when the text is missing.
  set(_os_major "")
  file(STRINGS "${_os_inc}/openssl/opensslv.h" _os_version_text REGEX "OPENSSL_VERSION_TEXT")
  if(_os_version_text MATCHES "OpenSSL ([0-9]+)")
    set(_os_major "${CMAKE_MATCH_1}")
  else()
    file(STRINGS "${_os_inc}/openssl/opensslv.h" _os_version_num REGEX "OPENSSL_VERSION_NUMBER")
    if(_os_version_num MATCHES "0x([0-9A-Fa-f])")
      string(SUBSTRING "${CMAKE_MATCH_1}" 0 1 _os_first)
      if(_os_first STREQUAL "3")
        set(_os_major 3)
      else()
        set(_os_major 1)
      endif()
    endif()
  endif()
  if(NOT _os_major)
    continue()
  endif()

  if(_os_major GREATER_EQUAL 3 AND _os_major GREATER _os_best_major)
    set(_os_best_major "${_os_major}")
    set(_os_best_root "${_root}")
    set(_os_best_inc "${_os_inc}")
    set(_os_best_ssl "${_os_ssl}")
    set(_os_best_crypto "${_os_crypto}")
  elseif(_os_major LESS 3 AND _os_major GREATER _os_best1_major)
    set(_os_best1_major "${_os_major}")
    set(_os_best1_root "${_root}")
    set(_os_best1_inc "${_os_inc}")
    set(_os_best1_ssl "${_os_ssl}")
    set(_os_best1_crypto "${_os_crypto}")
  endif()
endforeach()

set(_os_use_major -1)
if(NDMSPC_OPENSSL STREQUAL "env")
  set(_os_use_major "${_os_best_major}")
elseif(NDMSPC_OPENSSL STREQUAL "3")
  set(_os_use_major "${_os_best_major}")
elseif(NDMSPC_OPENSSL STREQUAL "1.1")
  set(_os_use_major "${_os_best1_major}")
else()
  if(_os_best_major GREATER_EQUAL 3)
    set(_os_use_major "${_os_best_major}")
  elseif(NDMSPC_OPENSSL_ALLOW_1_1)
    set(_os_use_major "${_os_best1_major}")
  endif()
endif()

if(_os_use_major LESS 0)
  if(NDMSPC_OPENSSL STREQUAL "env")
    message(FATAL_ERROR
      "NDMSPC_OPENSSL=env requires OPENSSL_ROOT or OPENSSL_ROOT_DIR to point to "
      "OpenSSL >= 3.0 (found: '${_openssl_roots}').")
  endif()
  if(NDMSPC_OPENSSL STREQUAL "1.1")
    message(FATAL_ERROR
      "OpenSSL 1.1 was requested (NDMSPC_OPENSSL=1.1) but none was found. "
      "Set OPENSSL_ROOT_DIR or $ENV{OPENSSL_ROOT} to a 1.1 installation.")
  endif()
  message(FATAL_ERROR
    "OpenSSL >= 3.0 is required by the bundled httplib (v0.54.1) and was not found in "
    "/usr, /usr/local, OPENSSL_ROOT_DIR or $ENV{OPENSSL_ROOT}. "
    "Set -DOPENSSL_ROOT_DIR=/path/to/openssl3, or -DNDMSPC_OPENSSL=1.1 for the "
    "compatibility build.")
endif()

if(_os_use_major GREATER_EQUAL 3)
  set(_os_use_root "${_os_best_root}")
  set(_os_use_inc "${_os_best_inc}")
  set(_os_use_ssl "${_os_best_ssl}")
  set(_os_use_crypto "${_os_best_crypto}")
else()
  set(_os_use_root "${_os_best1_root}")
  set(_os_use_inc "${_os_best1_inc}")
  set(_os_use_ssl "${_os_best1_ssl}")
  set(_os_use_crypto "${_os_best1_crypto}")
endif()

set(OpenSSL_FOUND TRUE)
set(OpenSSL_VERSION_MAJOR "${_os_use_major}")
set(NDMSPC_OPENSSL_MAJOR "${_os_use_major}")
set(OpenSSL_INCLUDE_DIRS "${_os_use_inc}")
set(OpenSSL_SSL_LIBRARY "${_os_use_ssl}")
set(OpenSSL_CRYPTO_LIBRARY "${_os_use_crypto}")
set(OpenSSL_LIBRARIES "${_os_use_ssl}" "${_os_use_crypto}")

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

message(STATUS "OpenSSL ${OpenSSL_VERSION_MAJOR}.x selected from '${_os_use_root}' (${OpenSSL_INCLUDE_DIRS})")
if(OpenSSL_VERSION_MAJOR LESS 3)
  message(STATUS "OpenSSL: 1.1 compatibility mode is active")
endif()

mark_as_advanced(OpenSSL_INCLUDE_DIRS OpenSSL_SSL_LIBRARY OpenSSL_CRYPTO_LIBRARY)
