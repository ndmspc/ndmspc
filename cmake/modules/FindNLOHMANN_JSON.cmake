# FindNLOHMANN_JSON.cmake
#
# This module defines
# NLOHMANN_JSON_FOUND, whether NLOHMANN_JSON was found
# NLOHMANN_JSON_INCLUDE_DIRS, include directories
# NLOHMANN_JSON_LIBRARIES, link libraries

find_path(NLOHMANN_JSON_INCLUDE_DIRS
  NAMES nlohmann/json.hpp
  PATHS ${NLOHMANN_JSON_ROOT}/include /usr/local/include /usr/include
)

# find_library(NLOHMANN_JSON_LIBRARIES
#   NAMES websockets
#   PATHS ${NLOHMANN_JSON_ROOT}/lib /usr/local/lib /usr/lib
# )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NLOHMANN_JSON DEFAULT_MSG
                                  NLOHMANN_JSON_INCLUDE_DIRS)

mark_as_advanced(NLOHMANN_JSON_INCLUDE_DIRS)
