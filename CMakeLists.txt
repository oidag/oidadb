cmake_minimum_required(VERSION 3.21)

project(edb C)

set(CMAKE_C_STANDARD 17)

# liboidadb
file(GLOB liboidadb_src c.src/*.c)
include_directories(c.src/include)
add_library(liboidadb ${liboidadb_src})

# odbm
file(GLOB_RECURSE odbm_src c.src/odbm/*.c)
add_executable(odbm ${odbm_src})
target_link_libraries(odbm GL glfw drawtext freetype m)

# tests
set(stdtestlibs pthread rt)
set(stdtestfiles c.src/errors.c c.src/odbspec.c)

add_executable       (t0001_io ${stdtestfiles} c.src/tests/t0001_io.c c.src/edbh.c c.src/edbd.c c.src/edbd-init.c)
target_link_libraries(t0001_io ${stdtestlibs})
