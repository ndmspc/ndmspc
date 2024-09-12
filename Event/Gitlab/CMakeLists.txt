set(PACKAGE NdmspcGitlabEvent)
file(GLOB SRCS "*.cxx")
set (MY_INCLUDE_DIRS
)
include_directories(${MY_INCLUDE_DIRS})
set(MY_EXTERNAL_LIBS
)
RootLib(${PACKAGE} "${SRCS}" "${MY_EXTERNAL_LIBS}" "")
