find_path(JWT_CPP_INCLUDE_DIRS
  NAMES jwt-cpp/jwt.h
  PATHS ${JWT_CPP_ROOT}/include /usr/local/include /usr/include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JWT_CPP DEFAULT_MSG JWT_CPP_INCLUDE_DIRS)

mark_as_advanced(JWT_CPP_INCLUDE_DIRS)
