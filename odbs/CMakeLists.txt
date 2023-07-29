

file(GLOB src *.c)
include_directories(../include)
add_executable(odbs ${src})
target_link_libraries(odbs oidadb)