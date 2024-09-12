set(PACKAGE NdmspcHepEvent)
file(GLOB SRCS "*.cxx")
RootLib(${PACKAGE} "${SRCS}" "" "")
