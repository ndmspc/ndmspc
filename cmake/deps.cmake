find_package(NLOHMANN_JSON REQUIRED)


# if(WITH_OPENTELEMETRY)
#   # TODO: Remove it: Temporary fix for opentelemetry-cpp
#   add_definitions(-Wno-cpp)
#   message(STATUS "Compiling with OpenTelemetry support")
#   find_package(opentelemetry-cpp CONFIG REQUIRED)
#   message(STATUS "Found opentelemetry-cpp: ${opentelemetry-cpp_VERSION}")
# endif()




message(STATUS "Compiling with ZeroMQ IPC support")
find_package(ZEROMQ REQUIRED)

find_package(Root REQUIRED)
include(ROOTMacros)
