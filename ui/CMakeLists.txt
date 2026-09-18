# The web UI is not built from sources here. The ndmspc-ui project publishes a
# ready-to-serve page tarball (index.html + assets/) to its GitLab Generic
# Package Registry, and this project downloads the requested version and
# installs it as the server's static root.
#
# `next` (the default) is republished from ndmspc-ui main on every push; a tag
# such as v1.2.3 pins a release. No node/npm toolchain is needed.
#
# See https://gitlab.com/ndmspc/ndmspc-ui for the available versions.

# Version of the prebuilt ndmspc-ui page to install. Selectable with
# -DNDMSPC_UI_VERSION=..., the NDMSPC_UI_VERSION environment variable, or
# `scripts/make.sh ui=<version>`.
if(DEFINED ENV{NDMSPC_UI_VERSION} AND NOT "$ENV{NDMSPC_UI_VERSION}" STREQUAL "")
  set(_ndmspc_ui_default_version "$ENV{NDMSPC_UI_VERSION}")
else()
  set(_ndmspc_ui_default_version "next")
endif()
set(NDMSPC_UI_VERSION "${_ndmspc_ui_default_version}" CACHE STRING "ndmspc-ui page version to download (e.g. next, v1.2.3)")

# ndmspc-ui uploads the page tarball to its Generic Package Registry; the same
# path serves `next` and released tags.
set(NDMSPC_UI_PAGE_URL
    "https://gitlab.com/api/v4/projects/ndmspc%2Fndmspc-ui/packages/generic/ndmspc-ui-page/${NDMSPC_UI_VERSION}/ndmspc-ui-page.tar.gz")

set(NDMSPC_UI_ARCHIVE ${CMAKE_BINARY_DIR}/ndmspc-ui-page.tar.gz)
# The tarball holds the page at its root; it is extracted here and this
# directory is what gets installed as the server's static root.
set(NDMSPC_UI_BUILD_OUTPUT_DIR ${CMAKE_BINARY_DIR}/ndmspc-ui)

# --- Download and unpack the page ---
# Fetched on every configure so `next` tracks the latest ndmspc-ui main build.
message(STATUS "Fetching ndmspc-ui page '${NDMSPC_UI_VERSION}' from ${NDMSPC_UI_PAGE_URL}")

file(DOWNLOAD ${NDMSPC_UI_PAGE_URL} ${NDMSPC_UI_ARCHIVE}
    STATUS NDMSPC_UI_DOWNLOAD_STATUS
    TLS_VERIFY ON)
list(GET NDMSPC_UI_DOWNLOAD_STATUS 0 NDMSPC_UI_DOWNLOAD_CODE)
list(GET NDMSPC_UI_DOWNLOAD_STATUS 1 NDMSPC_UI_DOWNLOAD_MESSAGE)
if(NOT NDMSPC_UI_DOWNLOAD_CODE EQUAL 0)
  message(FATAL_ERROR
      "Failed to download ndmspc-ui page '${NDMSPC_UI_VERSION}' from ${NDMSPC_UI_PAGE_URL}: ${NDMSPC_UI_DOWNLOAD_MESSAGE}")
endif()

# Unpack into a clean directory so switching versions cannot leave stale assets.
file(REMOVE_RECURSE ${NDMSPC_UI_BUILD_OUTPUT_DIR})
file(ARCHIVE_EXTRACT INPUT ${NDMSPC_UI_ARCHIVE} DESTINATION ${NDMSPC_UI_BUILD_OUTPUT_DIR})

if(NOT EXISTS ${NDMSPC_UI_BUILD_OUTPUT_DIR}/index.html)
  message(FATAL_ERROR
      "ndmspc-ui page '${NDMSPC_UI_VERSION}' does not contain index.html; does that version exist in the ndmspc-ui package registry?")
endif()

# --- Clean Target ---
add_custom_target(clean_ndmspc_ui
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NDMSPC_UI_BUILD_OUTPUT_DIR} ${NDMSPC_UI_ARCHIVE}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning downloaded ndmspc-ui page"
    VERBATIM
)

INSTALL(DIRECTORY ${NDMSPC_UI_BUILD_OUTPUT_DIR} DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME})
