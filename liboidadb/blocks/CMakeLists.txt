
# liboidadb
#
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
file(GLOB src *.c)
include_directories(../../include)
add_library(oidadb SHARED ${src})

add_subdirectory(test)
