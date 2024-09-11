set(PACKAGE NdmspcCore)
file(GLOB SRCS "*.cxx")

set(MY_EXTERNAL_LIBS
)

RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")
