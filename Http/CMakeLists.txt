set(PACKAGE NdmspcHttp)
file(GLOB SRCS "*.cxx")

set(MY_EXTERNAL_LIBS
RHTTP
)

RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")
