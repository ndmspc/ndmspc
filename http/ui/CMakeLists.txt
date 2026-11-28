# The web UI is not built from sources here. The ndmspc-ui project publishes a
# ready-to-serve page tarball (index.html + assets/) to its GitLab Generic
# Package Registry, and this project downloads the requested version and
# installs it as the server's static root.
#
# A tag build asks for the <major>.<minor>-stable slot (v1.4-stable, the newest
# 1.4.x page), so UI fixes reach an ndmspc release rebuild without tag churn; the
# exact tag page (v1.4.3) and finally `next` (the newest development build) are
# the fallbacks. An explicitly pinned NDMSPC_UI_VERSION is downloaded as-is, with
# no fallback. No node/npm toolchain is needed.
#
# A source tarball produced by `make dist` already carries the page
# (NDMSPC_UI_EMBED), so building from it needs no network; -DNDMSPC_UI_FETCH=ON
# (or a pinned NDMSPC_UI_VERSION) downloads the current page instead.
#
# See https://gitlab.com/ndmspc/ndmspc-ui for the available versions.

# NDMSPC_UI_VERSION, NDMSPC_UI_VERSION_PINNED and NDMSPC_UI_CANDIDATES are
# resolved in cmake/version.cmake; the candidate order lives there only.

set(NDMSPC_UI_BASE_URL
    "https://gitlab.com/api/v4/projects/ndmspc%2Fndmspc-ui/packages/generic/ndmspc-ui-page")

# Not `ndmspc-*.tar.gz`: that is the CPack source tarball's own name in this directory.
set(NDMSPC_UI_ARCHIVE ${CMAKE_BINARY_DIR}/ui-page.tar.gz)
# The tarball holds the page at its root; it is extracted here and this
# directory is what gets installed as the server's static root.
set(NDMSPC_UI_BUILD_OUTPUT_DIR ${CMAKE_BINARY_DIR}/ndmspc-ui)

set(NDMSPC_UI_EMBEDDED_ARCHIVE "${CMAKE_SOURCE_DIR}/http/ui/ndmspc-ui-page.tar.gz")
set(NDMSPC_UI_EMBEDDED_VERSION_FILE "${CMAKE_SOURCE_DIR}/http/ui/ndmspc-ui-page.version")
set(NDMSPC_UI_EMBEDDED_SHA256_FILE "${CMAKE_SOURCE_DIR}/http/ui/ndmspc-ui-page.sha256")

# The expected hash of the page archive: -D / $ENV wins, then the checksum `make dist` embeds
# beside the page. Empty means "not known" — the archive is then only as trustworthy as TLS,
# but the hash it did have is printed so it can be pinned from the log.
set(NDMSPC_UI_PAGE_SHA256_EXPECTED "")
if(DEFINED ENV{NDMSPC_UI_PAGE_SHA256} AND NOT "$ENV{NDMSPC_UI_PAGE_SHA256}" STREQUAL "")
  set(NDMSPC_UI_PAGE_SHA256_EXPECTED "$ENV{NDMSPC_UI_PAGE_SHA256}")
elseif(DEFINED NDMSPC_UI_PAGE_SHA256 AND NOT "${NDMSPC_UI_PAGE_SHA256}" STREQUAL "")
  set(NDMSPC_UI_PAGE_SHA256_EXPECTED "${NDMSPC_UI_PAGE_SHA256}")
endif()

set(NDMSPC_UI_VERSION_USED "")
if(NOT NDMSPC_UI_VERSION_PINNED
   AND NOT NDMSPC_UI_FETCH
   AND NOT EXISTS "${CMAKE_SOURCE_DIR}/.git"
   AND EXISTS "${NDMSPC_UI_EMBEDDED_ARCHIVE}")
  # Source tarball from `make dist`: it already carries the page.
  file(STRINGS "${NDMSPC_UI_EMBEDDED_VERSION_FILE}" NDMSPC_UI_VERSION_USED LIMIT_COUNT 1)
  if(NDMSPC_UI_VERSION_USED STREQUAL "")
    set(NDMSPC_UI_VERSION_USED "embedded")
  endif()
  message(STATUS
      "Using the ndmspc-ui page embedded in the source tarball ('${NDMSPC_UI_VERSION_USED}'); "
      "-DNDMSPC_UI_FETCH=ON downloads the current one instead")
  configure_file("${NDMSPC_UI_EMBEDDED_ARCHIVE}" "${NDMSPC_UI_ARCHIVE}" COPYONLY)

  # The embedded checksum is what `make dist` recorded when it downloaded this very page. It
  # catches a tarball that was cut open and re-packed with a different page, or corrupted
  # storage, without needing network access. An older tarball predating the file is accepted.
  if(NDMSPC_UI_PAGE_SHA256_EXPECTED STREQUAL ""
     AND EXISTS "${NDMSPC_UI_EMBEDDED_SHA256_FILE}")
    file(STRINGS "${NDMSPC_UI_EMBEDDED_SHA256_FILE}" _ui_embedded_sha_line LIMIT_COUNT 1)
    string(REGEX MATCH "^[0-9a-fA-F]+" NDMSPC_UI_PAGE_SHA256_EXPECTED "${_ui_embedded_sha_line}")
  endif()
  if(NDMSPC_UI_PAGE_SHA256_EXPECTED STREQUAL "")
    message(STATUS "  the tarball carries no http/ui/ndmspc-ui-page.sha256; the embedded page is not verified")
  endif()
else()
  # --- Download the page ---
  # Fetched on every configure so `next` tracks the latest ndmspc-ui build.
  foreach(_candidate IN LISTS NDMSPC_UI_CANDIDATES)
    set(_url "${NDMSPC_UI_BASE_URL}/${_candidate}/ndmspc-ui-page.tar.gz")
    message(STATUS "Fetching ndmspc-ui page '${_candidate}' from ${_url}")
    file(DOWNLOAD "${_url}" "${NDMSPC_UI_ARCHIVE}"
        STATUS _status
        TLS_VERIFY ON)
    list(GET _status 0 _code)
    list(GET _status 1 _message)
    if(_code EQUAL 0)
      set(NDMSPC_UI_VERSION_USED "${_candidate}")
      break()
    endif()
    message(STATUS "  '${_candidate}' is not available: ${_message}")
  endforeach()

  if(NDMSPC_UI_VERSION_USED STREQUAL "")
    string(REPLACE ";" ", " _tried "${NDMSPC_UI_CANDIDATES}")
    message(FATAL_ERROR
        "No ndmspc-ui page could be downloaded (tried: ${_tried}). "
        "See ${NDMSPC_UI_BASE_URL}/<version>/ndmspc-ui-page.tar.gz for the available versions.")
  endif()
  if(NOT NDMSPC_UI_VERSION_USED STREQUAL NDMSPC_UI_VERSION)
    message(STATUS "ndmspc-ui page '${NDMSPC_UI_VERSION}' is not published; using '${NDMSPC_UI_VERSION_USED}'")
  endif()
endif()

# The published slot (`v1.4-stable`, `next`) is republished, so it does not say which build the
# bytes are. The page stamps its own version at build time (VITE_APP_VERSION, from the tag in a
# release pipeline), which does; resolve it so the log and the embedded .version name the real
# build and not just the slot it came from.
set(NDMSPC_UI_PAGE_BUILD "")
if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/ui-page-tag.sh")
  execute_process(
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/ui-page-tag.sh" "${NDMSPC_UI_ARCHIVE}"
    OUTPUT_VARIABLE NDMSPC_UI_PAGE_BUILD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
# An embedded .version already reads "<slot> (<build>)", so only say it again when it does not.
if(NOT NDMSPC_UI_PAGE_BUILD STREQUAL "")
  string(FIND "${NDMSPC_UI_VERSION_USED}" "(${NDMSPC_UI_PAGE_BUILD})" _ui_build_shown)
  if(_ui_build_shown EQUAL -1)
    message(STATUS "ndmspc-ui page '${NDMSPC_UI_VERSION_USED}' is build ${NDMSPC_UI_PAGE_BUILD}")
  endif()
endif()

# Verify whatever page we are about to serve and report the hash, so every configure states the
# exact bytes it used and a pinned hash is enforced (a floating -stable/next slot cannot be
# pinned, but the value printed here is what a later pin is seeded from).
file(SHA256 "${NDMSPC_UI_ARCHIVE}" _ui_sha_actual)
if(NDMSPC_UI_PAGE_SHA256_EXPECTED STREQUAL "")
  message(STATUS "ndmspc-ui page '${NDMSPC_UI_VERSION_USED}' sha256: ${_ui_sha_actual}")
else()
  string(TOLOWER "${NDMSPC_UI_PAGE_SHA256_EXPECTED}" _ui_sha_expected_lower)
  if(NOT _ui_sha_actual STREQUAL _ui_sha_expected_lower)
    message(FATAL_ERROR
        "ndmspc-ui page '${NDMSPC_UI_VERSION_USED}' does not match NDMSPC_UI_PAGE_SHA256.\n"
        "  expected: ${_ui_sha_expected_lower}\n"
        "  actual:   ${_ui_sha_actual}\n"
        "The archive was downloaded from ${NDMSPC_UI_BASE_URL}/${NDMSPC_UI_VERSION_USED}/ndmspc-ui-page.tar.gz "
        "(or came embedded in the source tarball). Refusing to install a page whose bytes differ.")
  endif()
  message(STATUS "ndmspc-ui page '${NDMSPC_UI_VERSION_USED}' sha256 verified: ${_ui_sha_actual}")
endif()

# Unpack into a clean directory so switching versions cannot leave stale assets.
file(REMOVE_RECURSE ${NDMSPC_UI_BUILD_OUTPUT_DIR})
file(ARCHIVE_EXTRACT INPUT ${NDMSPC_UI_ARCHIVE} DESTINATION ${NDMSPC_UI_BUILD_OUTPUT_DIR})

if(NOT EXISTS ${NDMSPC_UI_BUILD_OUTPUT_DIR}/index.html)
  message(FATAL_ERROR
      "ndmspc-ui page '${NDMSPC_UI_VERSION_USED}' does not contain index.html; is the package in the ndmspc-ui registry complete?")
endif()

# --- Clean Target ---
add_custom_target(clean_ndmspc_ui
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NDMSPC_UI_BUILD_OUTPUT_DIR} ${NDMSPC_UI_ARCHIVE}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning downloaded ndmspc-ui page"
    VERBATIM
)

INSTALL(DIRECTORY ${NDMSPC_UI_BUILD_OUTPUT_DIR} DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME})
