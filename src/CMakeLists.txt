set (MY_INCLUDE_DIRS
)
include_directories(${MY_INCLUDE_DIRS})

set(MY_EXTERNAL_LIBS
)

file(GLOB EXEC_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cxx" )
string(REPLACE ".cxx" "" EXECS "${EXEC_SRCS}")
foreach(t ${EXECS})
    RootBin(${t} "${t}.cxx" "${MY_EXTERNAL_LIBS}")
endforeach(t)

