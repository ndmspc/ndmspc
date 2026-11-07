set(MY_INCLUDE_DIRS
  ${HTTPLIB_INCLUDE_DIRS}
  ${OpenSSL_INCLUDE_DIRS}
  ${NLOHMANN_JSON_INCLUDE_DIRS}
  ${CLI11_INCLUDE_DIRS}
)

set(MY_EXTERNAL_LIBS
  ${HTTPLIB_PLATFORM_LIBS}
  ${OpenSSL_LIBRARIES}
  NdmspcCore
)

list(APPEND MY_INCLUDE_DIRS ${ZEROMQ_INCLUDE_DIRS})
list(APPEND MY_EXTERNAL_LIBS ${ZEROMQ_LIBRARIES})

include_directories(${MY_INCLUDE_DIRS})
file(GLOB EXEC_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cxx")
string(REPLACE ".cxx" "" EXECS "${EXEC_SRCS}")
foreach(t ${EXECS})
  RootBin(${t} "${t}.cxx" "${MY_EXTERNAL_LIBS}")
  # The record feature talks to a room over `https://` too, so the bundled httplib has to be built with
  # OpenSSL support (it is header-only: the define is what selects the TLS client).
  target_compile_definitions(${t} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endforeach(t)
