set(HTTPLIB_VERSION "v0.18.1")
# set(NLOHMANN_JSON_VERSION "v3.11.3")

find_package(NLOHMANN_JSON REQUIRED)

if (WITH_AI)
  find_package(OpenSSL REQUIRED)

option(USE_FAST_FETCH "Fast fetch mode" ON)

# ------------------ Dependencies via FetchContent ------------------
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Disable update step")

if(USE_FAST_FETCH)
    message(STATUS "FAST MODE: Downloading compressed archives")
    FetchContent_Declare(
            httplib
            URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/${HTTPLIB_VERSION}.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # FetchContent_Declare(
    #         nlohmann_json
    #         URL https://github.com/nlohmann/json/releases/download/${NLOHMANN_JSON_VERSION}/include.zip
    #         DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # )
else()
    message(STATUS "GIT MODE: Cloning full repositories")
    FetchContent_Declare(
            httplib
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
            GIT_TAG ${HTTPLIB_VERSION}
    )
    # FetchContent_Declare(
    #         nlohmann_json
    #         GIT_REPOSITORY https://github.com/nlohmann/json.git
    #         GIT_TAG ${NLOHMANN_JSON_VERSION}
    # )
endif()

FetchContent_MakeAvailable(httplib)
# FetchContent_MakeAvailable(nlohmann_json)

# # Create interface target for json if using fast fetch
# if(USE_FAST_FETCH AND NOT TARGET nlohmann_json::nlohmann_json)
#     add_library(nlohmann_json INTERFACE)
#     target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_SOURCE_DIR}/include)
#     add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
# endif()
endif()

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
