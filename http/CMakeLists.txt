set(MODULE Http)
set(PACKAGE Ndmspc${MODULE})
# The sources live in feature subdirectories (auth/, room/, mcp/, ngnt/, server/), but the headers
# still install flat: a file in http/room/ is included as "ndmspc/http/NRoomRouter.h" like it always
# was, so no consumer and no user macro has to change. CONFIGURE_DEPENDS so that moving a file
# between the directories is picked up without a manual re-configure.
file(GLOB SRCS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx" "*/*.cxx")
# cli/ and tui/ are their own executables, already added below - they are not part of the library.
list(FILTER SRCS EXCLUDE REGEX "^(cli|tui|examples)/")

# No mirror of the feature directories under ${CMAKE_BINARY_DIR}/include is needed: RootLib copies
# the headers flat (and records those flat names in the dictionary), which is exactly the layout
# consumers include and ROOT autoloads by.

set(MY_INCLUDE_DIRS
  ${NLOHMANN_JSON_INCLUDE_DIRS}
  ${HTTPLIB_INCLUDE_DIRS}
  ${JWT_CPP_INCLUDE_DIRS}
  ${OpenSSL_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/core/cli
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  ${NLOHMANN_JSON_LIBRARIES}
  ${OpenSSL_LIBRARIES}
  RHTTP
  NdmspcCore
)

RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")
target_compile_definitions(${PACKAGE} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

add_subdirectory(cli)
add_subdirectory(tui)
