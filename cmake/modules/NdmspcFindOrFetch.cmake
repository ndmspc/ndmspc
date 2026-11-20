# NdmspcFindOrFetch.cmake
#
# Common helper to resolve a dependency in three steps:
#   1. find_package(<NAME>)               (system install, via cmake/modules/Find<NAME>.cmake)
#   2. FetchContent from a release tarball (USE_FAST_FETCH ON), taken from NDMSPC_DEPS_DIR
#                                          when `make dist` vendored it there, else downloaded
#   3. FetchContent from a git tag         (USE_FAST_FETCH OFF)
#
# Usage:
#   ndmspc_find_or_fetch(
#     NAME <NAME>                 # e.g. NLOHMANN_JSON, HTTPLIB
#     VERSION <tag>                # e.g. v3.11.3
#     URL <tarball-url>            # used when USE_FAST_FETCH is ON
#     URL_HASH <algo>=<digest>     # e.g. SHA256=<hex>: verify the downloaded archive (required
#                                  # for vendoring: the local copy is checked against it too)
#     GIT_REPOSITORY <git-url>     # used when USE_FAST_FETCH is OFF, tag = VERSION
#     INCLUDE_SUBDIR <path>        # path under the fetched source with the headers, "." for repo root
#     [REQUIRED]
#     [NO_ADD_SUBDIRECTORY]        # fetch only (no add_subdirectory): expose headers only,
#                                  # do not register the dependency's own install() rules
#   )
#
# On return sets, in the caller's scope: <NAME>_FOUND, <NAME>_INCLUDE_DIRS, <NAME>_LIBRARIES

include_guard(GLOBAL)

function(ndmspc_find_or_fetch)
  set(options REQUIRED NO_ADD_SUBDIRECTORY)
  set(oneValueArgs NAME VERSION URL URL_HASH GIT_REPOSITORY INCLUDE_SUBDIR)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

  string(TOLOWER ${ARG_NAME} _name_lower)

  option(USE_SYSTEM_${ARG_NAME} "Use system-installed ${ARG_NAME}" ON)

  if(USE_SYSTEM_${ARG_NAME})
    find_package(${ARG_NAME} QUIET)
  endif()

  if(${ARG_NAME}_FOUND)
    message(STATUS "${ARG_NAME}: resolved via system (${${ARG_NAME}_INCLUDE_DIRS})")
    return()
  endif()

  if(NOT ARG_URL AND NOT ARG_GIT_REPOSITORY)
    if(ARG_REQUIRED)
      message(FATAL_ERROR "${ARG_NAME} is required but not found on the system, and no URL/GIT_REPOSITORY was given for fetching")
    endif()
    return()
  endif()

  include(FetchContent)

  if(ARG_NO_ADD_SUBDIRECTORY)
    # Point at a directory without a CMakeLists.txt so FetchContent_MakeAvailable
    # downloads but does not add the dependency's CMake subproject (see below).
    set(_source_subdir_arg SOURCE_SUBDIR cmake/no-add-subdirectory-sentinel)
  else()
    set(_source_subdir_arg "")
  endif()

  if(USE_FAST_FETCH)
    # URL_HASH makes FetchContent (and ExternalProject's download step) reject an archive
    # whose bytes differ from the pinned release: a hijacked mirror, a retagged release or a
    # truncated download then fails the configure instead of being compiled.
    set(_url_hash_arg "")
    set(_hash_short "unverified")
    if(ARG_URL_HASH)
      set(_url_hash_arg URL_HASH ${ARG_URL_HASH})
      string(REGEX REPLACE "^[^=]*=" "" _hash_short "${ARG_URL_HASH}")
    else()
      message(WARNING
        "${ARG_NAME}: no URL_HASH given for ${ARG_URL}; the downloaded archive is not verified")
    endif()

    # Pre-downloaded archive first (a source tarball from `make dist`, or a checkout that kept
    # third-party/), and only then the network: a production build configures offline and never
    # re-downloads what it already has. The local copy is hashed like a download, so an archive
    # that does not match the pin is refused instead of compiled.
    set(_dep_url ${ARG_URL})
    set(_dep_origin "download")
    if(NDMSPC_DEPS_DIR)
      get_filename_component(_url_basename "${ARG_URL}" NAME)
      set(_vendored "${NDMSPC_DEPS_DIR}/${_name_lower}/${_url_basename}")
      if(EXISTS "${_vendored}")
        if(NOT ARG_URL_HASH)
          message(FATAL_ERROR
            "${ARG_NAME}: found the pre-downloaded archive '${_vendored}' but no URL_HASH to "
            "verify it against; pin one in cmake/deps.cmake.")
        endif()
        # ARG_URL_HASH is "<ALGO>=<hex>"; file() takes the algorithm as its first argument.
        string(REGEX MATCH "^[^=]*" _hash_algo "${ARG_URL_HASH}")
        string(REGEX REPLACE "^[^=]*=" "" _vendored_expected "${ARG_URL_HASH}")
        file(${_hash_algo} "${_vendored}" _vendored_actual)
        string(TOLOWER "${_vendored_expected}" _vendored_expected)
        string(TOLOWER "${_vendored_actual}" _vendored_actual_lower)
        if(NOT _vendored_actual_lower STREQUAL _vendored_expected)
          message(FATAL_ERROR
            "${ARG_NAME}: the pre-downloaded archive '${_vendored}' does not match the pinned hash.\n"
            "  expected: ${_vendored_expected}\n"
            "  actual:   ${_vendored_actual_lower}\n"
            "Re-run `make dist` (or delete the file) to vendor the pinned release again.")
        endif()
        set(_dep_url "${_vendored}")
        set(_dep_origin "pre-downloaded")
      endif()
    endif()

    message(STATUS
      "${ARG_NAME}: resolved via FetchContent tarball (${ARG_VERSION}, ${_dep_origin}, sha256 ${_hash_short})")
    FetchContent_Declare(
      ${_name_lower}
      URL ${_dep_url}
      ${_url_hash_arg}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      ${_source_subdir_arg}
    )
  else()
    message(STATUS "${ARG_NAME}: resolved via FetchContent git tag (${ARG_VERSION})")
    FetchContent_Declare(
      ${_name_lower}
      GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
      GIT_TAG ${ARG_VERSION}
      ${_source_subdir_arg}
    )
  endif()

  # Download-only mode (NO_ADD_SUBDIRECTORY): the SOURCE_SUBDIR sentinel above makes
  # FetchContent_MakeAvailable skip add_subdirectory(), so header-only dependencies
  # never register their own install() rules in the NDMSPC install.
  FetchContent_MakeAvailable(${_name_lower})

  if(ARG_INCLUDE_SUBDIR STREQUAL ".")
    set(_include_dirs "${${_name_lower}_SOURCE_DIR}")
  else()
    set(_include_dirs "${${_name_lower}_SOURCE_DIR}/${ARG_INCLUDE_SUBDIR}")
  endif()

  set(${ARG_NAME}_INCLUDE_DIRS "${_include_dirs}" PARENT_SCOPE)
  set(${ARG_NAME}_LIBRARIES "" PARENT_SCOPE)
  set(${ARG_NAME}_FOUND TRUE PARENT_SCOPE)
endfunction()
