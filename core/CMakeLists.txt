set(MODULE Core)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")
set(MY_INCLUDE_DIRS
  ${NLOHMANN_JSON_INCLUDE_DIRS}
  ${ZEROMQ_INCLUDE_DIRS}
)
set(MY_EXTERNAL_LIBS
  #  nlohmann_json::nlohmann_json
  ${ZEROMQ_LIBRARIES}
)

include_directories(${MY_INCLUDE_DIRS})
RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

add_subdirectory(cli)

