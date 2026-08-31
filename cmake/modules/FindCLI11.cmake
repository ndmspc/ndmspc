# FindCLI11.cmake
#
# This module defines
# CLI11_FOUND, whether CLI11 was found
# CLI11_INCLUDE_DIRS, include directories

find_path(CLI11_INCLUDE_DIRS
  NAMES CLI/CLI.hpp
  PATHS ${CLI11_ROOT}/include /usr/local/include /usr/include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CLI11 DEFAULT_MSG CLI11_INCLUDE_DIRS)

mark_as_advanced(CLI11_INCLUDE_DIRS)