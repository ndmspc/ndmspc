set(MODULE Taxi)
set(PACKAGE Ndmspc${MODULE})
file(GLOB SRCS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")

message(STATUS "Compiling with Parquet support")
set(ARROW_WITH_PARQUET ON)
find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

set(MY_INCLUDE_DIRS
  ${Arrow_INCLUDE_DIRS}
  ${Parquet_INCLUDE_DIRS}
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
  Arrow::arrow_shared
  Parquet::parquet_shared
  NdmspcCore
)

RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")

