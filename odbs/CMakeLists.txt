

file(GLOB src *.c)
include_directories(../include)
add_executable(odbs ${src})