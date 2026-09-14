
include(ExternalProject)
set(NPM_EXECUTABLE "npm") # Ensure npm is in the PATH or specify the full path to npm
# Define directories
set(NDMSPC_UI_SRC_DIR ${CMAKE_BINARY_DIR}/ndmspc-ui-src)
set(NDMSPC_UI_BUILD_OUTPUT_DIR ${CMAKE_BINARY_DIR}/ndmspc-ui)

# Determine the actual output directory from ndmspc-ui's package.json
set(NDMSPC_UI_PRODUCED_OUTPUT_DIR "dist") # <<< IMPORTANT: VERIFY AND ADJUST THIS

# Create a marker file for build completion
set(NDMSPC_UI_BUILD_MARKER ${NDMSPC_UI_SRC_DIR}/npm_build_completed.marker)
set(NDMSPC_UI_NPM_BUILD_LOG ${CMAKE_BINARY_DIR}/npm_build.log)

# --- External Project Setup for Cloning ---
ExternalProject_Add(
    ndmspc_ui_clone
    GIT_REPOSITORY https://gitlab.com/ndmspc/ndmspc-ui.git
    GIT_TAG main
    UPDATE_COMMAND ""
    SOURCE_DIR ${NDMSPC_UI_SRC_DIR}
    INSTALL_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    TEST_COMMAND ""
)

# --- Custom Command for npm install and npm run build ---
add_custom_command(
    OUTPUT ${NDMSPC_UI_BUILD_MARKER}
    DEPENDS ndmspc_ui_clone

    # One descriptive COMMENT for the entire set of commands
    COMMENT "Installing and building ndmspc-ui with npm"

    COMMAND ${CMAKE_COMMAND} -E rm -f ${NDMSPC_UI_BUILD_MARKER}
    WORKING_DIRECTORY ${NDMSPC_UI_SRC_DIR}
    VERBATIM

    # --include=dev keeps devDependencies when NODE_ENV=production is set;
    # --legacy-peer-deps works around @react-three/fiber peer react ">=19 <19.3"
    # while ndmspc-ui allows react ^19.2.6 (resolving to 19.3).
    COMMAND ${NPM_EXECUTABLE} install --silent --include=dev --legacy-peer-deps
    WORKING_DIRECTORY ${NDMSPC_UI_SRC_DIR}
    VERBATIM

    COMMAND sh -c "${NPM_EXECUTABLE} run build --silent > ${NDMSPC_UI_NPM_BUILD_LOG} 2>&1"
    WORKING_DIRECTORY ${NDMSPC_UI_SRC_DIR}
    VERBATIM

    COMMAND ${CMAKE_COMMAND} -E touch ${NDMSPC_UI_BUILD_MARKER}
    WORKING_DIRECTORY ${NDMSPC_UI_SRC_DIR}
    VERBATIM
)

# --- Custom Target to Copy Built Assets ---
add_custom_target(build_ndmspc_ui ALL
    DEPENDS ${NDMSPC_UI_BUILD_MARKER}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${NDMSPC_UI_SRC_DIR}/${NDMSPC_UI_PRODUCED_OUTPUT_DIR}
        ${NDMSPC_UI_BUILD_OUTPUT_DIR}
    COMMENT "Copying built UI assets from ${NDMSPC_UI_SRC_DIR}/${NDMSPC_UI_PRODUCED_OUTPUT_DIR} to ${NDMSPC_UI_BUILD_OUTPUT_DIR}"
    VERBATIM
)

# --- Clean Target ---
add_custom_target(clean_ndmspc_ui
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NDMSPC_UI_BUILD_OUTPUT_DIR} ${NDMSPC_UI_SRC_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning ndmspc-ui build artifacts"
    VERBATIM
)

INSTALL(DIRECTORY ${NDMSPC_UI_BUILD_OUTPUT_DIR} DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME})
