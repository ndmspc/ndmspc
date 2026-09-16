# add a target to generate API documentation with Doxygen
find_package(Doxygen)

if(NOT DOXYGEN_FOUND)
  message(FATAL_ERROR "Doxygen is needed to build the documentation.")
endif()

include(FetchContent)
FetchContent_Declare(
  doxygen-awesome-css
  URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/heads/main.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(doxygen-awesome-css)

# Save the location the files were cloned into
# This allows us to get the path to doxygen-awesome.css
FetchContent_GetProperties(doxygen-awesome-css SOURCE_DIR AWESOME_CSS_DIR)

# Generate the Doxyfile
# set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile.in)
# set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
# configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

# Optionally turn Doxygen warnings into build errors. Off by default so that
# warnings emitted only by a different Doxygen version cannot break the build;
# enable with -DDOCUMENTATION_WARN_AS_ERROR=ON (e.g. in CI) to enforce them.
if(DOCUMENTATION_WARN_AS_ERROR)
  set(DOXYGEN_WARN_AS_ERROR "FAIL_ON_WARNINGS")
else()
  set(DOXYGEN_WARN_AS_ERROR "")
endif()

set(doxy_main_page ${CMAKE_SOURCE_DIR}/README.md)
set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.Doxyfile.in)
set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.Doxyfile)

configure_file(${doxyfile_in} ${doxyfile} @ONLY)

add_custom_target(doc ALL
      COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html DESTINATION share/doc/${CMAKE_PROJECT_NAME})
