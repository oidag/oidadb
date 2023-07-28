cmake_minimum_required(VERSION 3.21)

project(liboidadb C)

set(CMAKE_C_STANDARD 17)

# liboidadb
file(GLOB liboidadb_src c.src/*.c)
include_directories(c.src/include)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options("-fvisibility=hidden")
    add_link_options("-s")
endif()
add_library(oidadb SHARED ${liboidadb_src})

# odbm
file(GLOB_RECURSE odbm_src c.src/odbm/*.c)
add_executable(odbm ${odbm_src})
target_link_libraries(odbm GL glfw drawtext freetype m)

add_subdirectory(c.src/tests)

#add_executable       (edbd_00 ${stdtestfiles} c.src/tests/20-edbx_00_t.c c.src/edbh.c c.src/edbd.c c.src/edbd-init.c)
#target_link_libraries(t0091_io ${stdtestlibs})

# t0005
#add_executable       (t0005_host ${liboidadb_src}
#        c.src/tests/20-edbx_05_t.c)

#target_link_libraries(t0005_host ${stdtestlibs})
