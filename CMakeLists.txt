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

# all tests
set(stdtestlibs pthread rt)
set(stdtestfiles c.src/errors.c c.src/odbspec.c c.src/telemetry.c)

# edbd
file(GLOB edbd_t c.src/edbd*.c)
add_executable(edbd_00 ${stdtestfiles}
        c.src/tests/10-edbd_00_t.c
        ${edbd_t})
target_link_libraries(edbd_00 ${stdtestlibs})

#edbl
file(GLOB edbl_t c.src/edbl*.c)
add_executable(edbl_00 ${stdtestfiles}
        c.src/tests/10-edbl_00_t.c
        ${edbl_t} ${edbd_t})
target_link_libraries(edbl_00 ${stdtestlibs})
add_executable(edbl_01 ${stdtestfiles}
        c.src/tests/10-edbl_01_t.c
        ${edbl_t} ${edbd_t})

#edbs
file(GLOB edbs_t c.src/edbs*.c)
add_executable(edbs_00 ${stdtestfiles}
        c.src/tests/15-edbs_00_t.c
        ${edbs_t} ${edbd_t})
add_executable(edbs_01 ${stdtestfiles}
        c.src/tests/15-edbs_01_t.c
        ${edbs_t} ${edbd_t})

# edbp
file(GLOB edbp_t c.src/edbp*.c)
add_executable(edbp_00 ${stdtestfiles}
        c.src/tests/15-edbp_00_t.c
        ${edbd_t}
        ${edbp_t})
target_link_libraries(edbp_00 ${stdtestlibs})

#edba
file(GLOB edba_t c.src/edba*.c)
add_executable(edba_00 ${stdtestfiles}
        c.src/tests/17-edba_00_t.c
        ${edba_t}
        ${edbl_t}
        ${edbd_t}
        ${edbp_t})


#add_executable       (edbd_00 ${stdtestfiles} c.src/tests/20-edbx_00_t.c c.src/edbh.c c.src/edbd.c c.src/edbd-init.c)
#target_link_libraries(t0091_io ${stdtestlibs})

# t0005
#add_executable       (t0005_host ${liboidadb_src}
#        c.src/tests/20-edbx_05_t.c)

#target_link_libraries(t0005_host ${stdtestlibs})
