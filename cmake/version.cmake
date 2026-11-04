# Version resolution: the single place that decides what version the build reports.
# Included from the top-level CMakeLists.txt before project(), so PROJECT_VERSION is
# known to project(VERSION ...) and NDMSPC_VERSION/NDMSPC_VERSION_RELEASE are ready for
# cmake/ndmspc.h.in and cmake/ndmspc.spec.in.
#
# Sources, first hit wins:
#   1. -DNDMSPC_VERSION=<version or git describe string>
#   2. -DPROJECT_VERSION (+ -DPROJECT_VERSION_RELEASE), kept for older callers
#   3. $ENV{NDMSPC_VERSION}
#   4. git describe in the source tree (a git checkout has .git)
#   5. the VERSION file (CPack source tarball, Docker build context: no .git)
#   6. .git_archival.txt (GitLab/GitHub "Download source code" archives)
#   7. 9999.0.0 (no version information at all)
#
# A "-dirty" suffix is dropped. Reported version: NDMSPC_VERSION is the tag without the
# leading "v" (a pre-release suffix such as 1.4.0-rc51 is kept), NDMSPC_VERSION_RELEASE is
# the short hash at an exact tag, "<commit count>-g<hash>" after one, or the pre-release
# suffix when the input is a plain version such as 1.4.0-rc6.

# The old CMakeLists.txt defaults were cached; a cache entry holding 9999.0.0 (or the
# release default 1) is a build directory from before this file existed, not a choice.
if(DEFINED PROJECT_VERSION AND PROJECT_VERSION STREQUAL "9999.0.0")
  unset(PROJECT_VERSION CACHE)
endif()
if(DEFINED PROJECT_VERSION_RELEASE AND PROJECT_VERSION_RELEASE STREQUAL "1")
  unset(PROJECT_VERSION_RELEASE CACHE)
endif()

# Splits "<tag>[-<n>-g<hash>][-dirty]", "<tag>-g<hash>" or a plain version into
# NDMSPC_VERSION/NDMSPC_VERSION_RELEASE and derives the numeric PROJECT_VERSION pair
# (which project() requires). An explicit PROJECT_VERSION / PROJECT_VERSION_RELEASE
# still wins over the derived value.
function(_ndmspc_apply_version_string raw source)
  string(STRIP "${raw}" _raw)
  string(REGEX REPLACE "^v" "" _raw "${_raw}")
  string(LENGTH "${_raw}" _raw_length)

  set(_tag "")
  set(_rel "")
  set(_at_tag FALSE)
  if(_raw MATCHES "^(.+)-([0-9]+)-g([0-9a-fA-F]+)(-dirty)?$")
    set(_tag "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_2 STREQUAL "0")
      set(_rel "${CMAKE_MATCH_3}")
      set(_at_tag TRUE)
    else()
      set(_rel "${CMAKE_MATCH_2}-g${CMAKE_MATCH_3}")
    endif()
  elseif(_raw MATCHES "^(.+)-g([0-9a-fA-F]+)(-dirty)?$")
    set(_tag "${CMAKE_MATCH_1}")
    set(_rel "${CMAKE_MATCH_2}")
  elseif(_raw_length GREATER_EQUAL 6 AND _raw MATCHES "^[0-9a-fA-F]+$")
    # git describe --always: the repository has no tag at all.
    set(_tag "9999.0.0")
    set(_rel "${_raw}")
  elseif(_raw MATCHES "^([0-9]+(\\.[0-9]+)*)(-(.+))?$")
    # Plain X.Y.Z[-pre], e.g. -DNDMSPC_VERSION=1.4.0-rc6 or a hand-written VERSION.
    # CMake's regex has no {n,m} repetition, hence the plain + above.
    set(_tag "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_4)
      set(_rel "${CMAKE_MATCH_4}")
    else()
      set(_at_tag TRUE)
    endif()
  else()
    set(_tag "${_raw}")
  endif()

  set(_project_version "${_tag}")
  set(_project_release "1")
  if(_tag MATCHES "^([0-9]+(\\.[0-9]+)*)(-(.+))?$")
    set(_project_version "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_4)
      set(_project_release "${CMAKE_MATCH_4}")
    endif()
  else()
    # project(VERSION ...) only accepts numbers; a non-numeric tag is reported through
    # NDMSPC_VERSION instead.
    set(_project_version "9999.0.0")
  endif()

  if(DEFINED PROJECT_VERSION AND NOT PROJECT_VERSION STREQUAL "")
    set(_project_version "${PROJECT_VERSION}")
  endif()
  if(DEFINED PROJECT_VERSION_RELEASE AND NOT PROJECT_VERSION_RELEASE STREQUAL "")
    set(_project_release "${PROJECT_VERSION_RELEASE}")
  endif()
  # Never report an empty release: the binaries print "<version>-<release>".
  if(_rel STREQUAL "")
    set(_rel "${_project_release}")
  endif()

  set(NDMSPC_VERSION "${_tag}" PARENT_SCOPE)
  set(NDMSPC_VERSION_RELEASE "${_rel}" PARENT_SCOPE)
  set(NDMSPC_VERSION_AT_TAG "${_at_tag}" PARENT_SCOPE)
  set(PROJECT_VERSION "${_project_version}" PARENT_SCOPE)
  set(PROJECT_VERSION_RELEASE "${_project_release}" PARENT_SCOPE)
  set(NDMSPC_VERSION_SOURCE "${source}" PARENT_SCOPE)
endfunction()

set(_ndmspc_version_string "")
set(_ndmspc_version_source "")

# An explicit -DNDMSPC_VERSION_RELEASE overrides the parsed commits/hash.
set(_ndmspc_release_override "")
if(DEFINED NDMSPC_VERSION_RELEASE AND NOT NDMSPC_VERSION_RELEASE STREQUAL "")
  set(_ndmspc_release_override "${NDMSPC_VERSION_RELEASE}")
endif()

if(DEFINED NDMSPC_VERSION AND NOT NDMSPC_VERSION STREQUAL "")
  set(_ndmspc_version_string "${NDMSPC_VERSION}")
  set(_ndmspc_version_source "command line")
elseif(DEFINED PROJECT_VERSION AND NOT PROJECT_VERSION STREQUAL "")
  # Legacy caller that only set -DPROJECT_VERSION.
  set(_ndmspc_version_string "${PROJECT_VERSION}")
  if(DEFINED PROJECT_VERSION_RELEASE AND NOT PROJECT_VERSION_RELEASE STREQUAL ""
     AND NOT PROJECT_VERSION_RELEASE STREQUAL "1")
    string(APPEND _ndmspc_version_string "-${PROJECT_VERSION_RELEASE}")
  endif()
  set(_ndmspc_version_source "command line")
elseif(DEFINED ENV{NDMSPC_VERSION} AND NOT "$ENV{NDMSPC_VERSION}" STREQUAL "")
  set(_ndmspc_version_string "$ENV{NDMSPC_VERSION}")
  set(_ndmspc_version_source "environment")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND git describe --tags --long --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _ndmspc_git_describe
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _ndmspc_git_result)
  if(_ndmspc_git_result EQUAL 0 AND NOT _ndmspc_git_describe STREQUAL "")
    set(_ndmspc_version_string "${_ndmspc_git_describe}")
    set(_ndmspc_version_source "git")
  else()
    execute_process(
      COMMAND git describe --always
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE _ndmspc_git_always
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _ndmspc_git_always_result)
    if(_ndmspc_git_always_result EQUAL 0 AND NOT _ndmspc_git_always STREQUAL "")
      set(_ndmspc_version_string "${_ndmspc_git_always}")
      set(_ndmspc_version_source "git (no tags)")
    endif()
  endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _ndmspc_version_file LIMIT_COUNT 1)
  if(NOT _ndmspc_version_file STREQUAL "")
    set(_ndmspc_version_string "${_ndmspc_version_file}")
    set(_ndmspc_version_source "VERSION file")
  endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git_archival.txt")
  # GitLab/GitHub source archive (git archive): export-subst expanded the placeholders,
  # see .gitattributes. A CPack tarball carries this file untouched and a VERSION file
  # instead, which is handled above; the $Format guard below skips it there.
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.git_archival.txt" _ndmspc_archival)
  set(_ndmspc_archival_describe "")
  set(_ndmspc_archival_node "")
  foreach(_line IN LISTS _ndmspc_archival)
    # Every MATCHES resets CMAKE_MATCH_*, so check for unexpanded placeholders (a CPack
    # tarball copies this file verbatim) first and consume each capture immediately.
    if(NOT _line MATCHES "\\$Format")
      if(_line MATCHES "^describe-name:[ \t]*(.+)$")
        string(STRIP "${CMAKE_MATCH_1}" _ndmspc_archival_describe)
      elseif(_line MATCHES "^node:[ \t]*([0-9a-fA-F]+)$")
        set(_ndmspc_archival_node "${CMAKE_MATCH_1}")
      endif()
    endif()
  endforeach()
  if(NOT _ndmspc_archival_describe STREQUAL "")
    # %(describe:tags) drops the "-0-g<hash>" part at an exact tag; put it back so the
    # archive reports the same version as `git describe --tags --long`.
    if(NOT _ndmspc_archival_describe MATCHES "-[0-9]+-g[0-9a-fA-F]+$"
       AND NOT _ndmspc_archival_node STREQUAL "")
      string(APPEND _ndmspc_archival_describe "-0-g${_ndmspc_archival_node}")
    endif()
    set(_ndmspc_version_string "${_ndmspc_archival_describe}")
    set(_ndmspc_version_source ".git_archival.txt")
  elseif(NOT _ndmspc_archival_node STREQUAL "")
    # No tag in the repository: report 9999.0.0-<hash>, like `git describe --always`.
    set(_ndmspc_version_string "${_ndmspc_archival_node}")
    set(_ndmspc_version_source ".git_archival.txt (no tags)")
  endif()
endif()

if(_ndmspc_version_string STREQUAL "")
  set(_ndmspc_version_string "9999.0.0")
  set(_ndmspc_version_source "fallback")
endif()

_ndmspc_apply_version_string("${_ndmspc_version_string}" "${_ndmspc_version_source}")
if(NOT _ndmspc_release_override STREQUAL "")
  set(NDMSPC_VERSION_RELEASE "${_ndmspc_release_override}")
endif()

message(STATUS
  "ndmspc version: ${NDMSPC_VERSION}-${NDMSPC_VERSION_RELEASE} (from ${NDMSPC_VERSION_SOURCE})")

# ndmspc-ui page version: a build from a release tag pins that tag, everything else
# (branches, pre-release tags such as 1.4.0-rc51) uses `next`. -DNDMSPC_UI_VERSION and
# the environment win. Defined here because cmake/ndmspc.spec.in bakes it and the spec
# is generated even when WITH_UI is off.
if(DEFINED ENV{NDMSPC_UI_VERSION} AND NOT "$ENV{NDMSPC_UI_VERSION}" STREQUAL "")
  set(_ndmspc_ui_default_version "$ENV{NDMSPC_UI_VERSION}")
else()
  set(_ndmspc_ui_default_version "next")
  if(NDMSPC_VERSION_AT_TAG AND NDMSPC_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    set(_ndmspc_ui_default_version "v${NDMSPC_VERSION}")
  endif()
endif()
set(NDMSPC_UI_VERSION "${_ndmspc_ui_default_version}" CACHE STRING
    "ndmspc-ui page version to download (e.g. next, v1.2.3)")
