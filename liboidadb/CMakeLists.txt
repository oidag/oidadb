
# liboidadb
#
file(GLOB src *.c)
include_directories(../include)
add_library(oidadb SHARED ${src})

#add_subdirectory(test)
