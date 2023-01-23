cmake_minimum_required(VERSION 3.21)

project(edb C)

set(CMAKE_C_STANDARD 17)

# liboidadb
file(GLOB liboidadb_src c.src/*.c)
include_directories(c.src/include)
add_library(liboidadb ${liboidadb_src})

# tests
add_executable(test_pages c.src/tests/page_main.c c.src/edbp.c c.src/edbd.c c.src/errors.c)
TARGET_LINK_LIBRARIES(test_pages pthread)
add_executable(test_host c.src/tests/host_main.c ${sources})
TARGET_LINK_LIBRARIES(test_host pthread rt)

# odbm
file(GLOB_RECURSE odbm_src c.src/odbm/*.c)
add_executable(odbm ${odbm_src})
target_link_libraries(odbm GL glfw drawtext freetype m)
