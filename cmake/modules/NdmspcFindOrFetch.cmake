# NdmspcFindOrFetch.cmake
#
# Common helper to resolve a dependency in three steps:
#   1. find_package(<NAME>)               (system install, via cmake/modules/Find<NAME>.cmake)
#   2. FetchContent from a release tarball (USE_FAST_FETCH ON)
#   3. FetchContent from a git tag         (USE_FAST_FETCH OFF)
#
# Usage:
#   ndmspc_find_or_fetch(
#     NAME <NAME>                 # e.g. NLOHMANN_JSON, HTTPLIB
#     VERSION <tag>                # e.g. v3.11.3
#     URL <tarball-url>            # used when USE_FAST_FETCH is ON
#     GIT_REPOSITORY <git-url>     # used when USE_FAST_FETCH is OFF, tag = VERSION
#     INCLUDE_SUBDIR <path>        # path under the fetched source with the headers, "." for repo root
#     [REQUIRED]
#   )
#
# On return sets, in the caller's scope: <NAME>_FOUND, <NAME>_INCLUDE_DIRS, <NAME>_LIBRARIES

include_guard(GLOBAL)

function(ndmspc_find_or_fetch)
  set(options REQUIRED)
  set(oneValueArgs NAME VERSION URL GIT_REPOSITORY INCLUDE_SUBDIR)
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

  if(USE_FAST_FETCH)
    message(STATUS "${ARG_NAME}: resolved via FetchContent tarball (${ARG_VERSION})")
    FetchContent_Declare(
      ${_name_lower}
      URL ${ARG_URL}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
  else()
    message(STATUS "${ARG_NAME}: resolved via FetchContent git tag (${ARG_VERSION})")
    FetchContent_Declare(
      ${_name_lower}
      GIT_REPOSITORY ${ARG_GIT_REPOSITORY}
      GIT_TAG ${ARG_VERSION}
    )
  endif()

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
